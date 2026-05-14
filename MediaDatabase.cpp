/**
 * @file MediaDatabase.cpp
 * @brief 媒体数据库实现
 */

#include "MediaDatabase.h"
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
std::string mysqlError(MYSQL* connection)
{
    if (!connection) return "数据库连接为空";
    return mysql_error(connection);
}

std::string quote(MYSQL* connection, const std::string& value)
{
    std::string escaped(value.size() * 2 + 1, '\0');
    unsigned long length = mysql_real_escape_string(
        connection,
        escaped.data(),
        value.c_str(),
        static_cast<unsigned long>(value.size())
    );
    escaped.resize(length);
    return "'" + escaped + "'";
}

std::string number(double value)
{
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

bool execute(MYSQL* connection, const std::string& sql, std::string& last_error, const std::string& prefix)
{
    if (!connection) {
        last_error = "未连接数据库";
        return false;
    }

    if (mysql_query(connection, sql.c_str()) != 0) {
        last_error = prefix + ": " + mysqlError(connection);
        return false;
    }

    return true;
}

MYSQL_RES* query(MYSQL* connection, const std::string& sql, std::string& last_error, const std::string& prefix)
{
    if (!execute(connection, sql, last_error, prefix)) {
        return nullptr;
    }

    MYSQL_RES* result = mysql_store_result(connection);
    if (!result && mysql_errno(connection) != 0) {
        last_error = prefix + ": " + mysqlError(connection);
        return nullptr;
    }

    return result;
}

std::string field(MYSQL_ROW row, unsigned long* lengths, unsigned int index)
{
    if (!row || !row[index]) return "";
    return std::string(row[index], lengths[index]);
}

int toInt(MYSQL_ROW row, unsigned int index)
{
    if (!row || !row[index]) return 0;
    return std::atoi(row[index]);
}

long long toInt64(MYSQL_ROW row, unsigned int index)
{
    if (!row || !row[index]) return 0;
    return std::strtoll(row[index], nullptr, 10);
}

double toDouble(MYSQL_ROW row, unsigned int index)
{
    if (!row || !row[index]) return 0.0;
    return std::strtod(row[index], nullptr);
}
}

MediaDatabase::MediaDatabase()
    : connection_(nullptr)
{
}

MediaDatabase::~MediaDatabase()
{
    disconnect();
}

// ============================================================
// 连接数据库
// ============================================================
bool MediaDatabase::connect(const std::string& host, const std::string& user,
                             const std::string& password, const std::string& database,
                             int port)
{
    disconnect();

    std::cout << "[Database] 正在初始化 MariaDB/MySQL 客户端..." << std::endl;
    connection_ = mysql_init(nullptr);
    if (!connection_) {
        last_error_ = "无法初始化 MariaDB/MySQL 客户端";
        return false;
    }

    std::string url = "tcp://" + host + ":" + std::to_string(port);
    std::cout << "[Database] 准备连接到: " << url << std::endl;

    MYSQL* connected = mysql_real_connect(
        connection_,
        host.c_str(),
        user.c_str(),
        password.c_str(),
        database.c_str(),
        static_cast<unsigned int>(port),
        nullptr,
        0
    );

    if (!connected) {
        last_error_ = std::string("连接失败: ") + mysqlError(connection_);
        std::cerr << "[Database] " << last_error_ << std::endl;
        mysql_close(connection_);
        connection_ = nullptr;
        return false;
    }

    if (mysql_set_character_set(connection_, "utf8mb4") != 0) {
        last_error_ = std::string("设置字符集失败: ") + mysqlError(connection_);
        std::cerr << "[Database] " << last_error_ << std::endl;
        mysql_close(connection_);
        connection_ = nullptr;
        return false;
    }

    database_name_ = database;
    std::cout << "[Database] 连接成功: " << host << " / " << database << std::endl;
    return true;
}

void MediaDatabase::disconnect()
{
    if (connection_) {
        mysql_close(connection_);
        connection_ = nullptr;
    }
}

// ============================================================
// 初始化数据库表
// ============================================================
bool MediaDatabase::initTables()
{
    if (!connection_) {
        last_error_ = "未连接数据库";
        return false;
    }

    const char* statements[] = {
        R"(
            CREATE TABLE IF NOT EXISTS media_library (
                id INT AUTO_INCREMENT PRIMARY KEY,
                file_path VARCHAR(500) NOT NULL UNIQUE,
                file_name VARCHAR(200),
                format VARCHAR(50),
                duration DOUBLE DEFAULT 0,
                file_size BIGINT DEFAULT 0,
                video_width INT DEFAULT 0,
                video_height INT DEFAULT 0,
                video_codec VARCHAR(50),
                video_frame_rate DOUBLE DEFAULT 0,
                video_bit_rate INT DEFAULT 0,
                audio_codec VARCHAR(50),
                audio_sample_rate INT DEFAULT 0,
                audio_channels INT DEFAULT 0,
                audio_bit_rate INT DEFAULT 0,
                title VARCHAR(200),
                tags VARCHAR(500),
                is_favorite TINYINT DEFAULT 0,
                play_count INT DEFAULT 0,
                added_time DATETIME DEFAULT CURRENT_TIMESTAMP,
                last_play_time DATETIME
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )",
        R"(
            CREATE TABLE IF NOT EXISTS play_history (
                id INT AUTO_INCREMENT PRIMARY KEY,
                media_id INT NOT NULL,
                play_time DATETIME DEFAULT CURRENT_TIMESTAMP,
                FOREIGN KEY (media_id) REFERENCES media_library(id) ON DELETE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )",
        R"(
            CREATE TABLE IF NOT EXISTS playlists (
                id INT AUTO_INCREMENT PRIMARY KEY,
                name VARCHAR(100) NOT NULL,
                created_time DATETIME DEFAULT CURRENT_TIMESTAMP
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )",
        R"(
            CREATE TABLE IF NOT EXISTS playlist_items (
                id INT AUTO_INCREMENT PRIMARY KEY,
                playlist_id INT NOT NULL,
                media_id INT NOT NULL,
                position INT DEFAULT 0,
                FOREIGN KEY (playlist_id) REFERENCES playlists(id) ON DELETE CASCADE,
                FOREIGN KEY (media_id) REFERENCES media_library(id) ON DELETE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )"
    };

    for (const char* statement : statements) {
        if (!execute(connection_, statement, last_error_, "建表失败")) {
            std::cerr << "[Database] " << last_error_ << std::endl;
            return false;
        }
    }

    std::cout << "[Database] 表初始化成功" << std::endl;
    return true;
}

// ============================================================
// 添加媒体文件
// ============================================================
int MediaDatabase::addMedia(const MediaInfo& info)
{
    if (!connection_) return -1;

    std::string title = info.title.empty() ? info.file_name : info.title;
    std::ostringstream sql;
    sql << "INSERT INTO media_library "
        << "(file_path, file_name, format, duration, file_size, "
        << "video_width, video_height, video_codec, video_frame_rate, video_bit_rate, "
        << "audio_codec, audio_sample_rate, audio_channels, audio_bit_rate, "
        << "title, tags) VALUES ("
        << quote(connection_, info.file_path) << ", "
        << quote(connection_, info.file_name) << ", "
        << quote(connection_, info.format_name) << ", "
        << number(info.duration) << ", "
        << info.file_size << ", "
        << info.video_info.width << ", "
        << info.video_info.height << ", "
        << quote(connection_, info.video_info.codec_name) << ", "
        << number(info.video_info.frame_rate) << ", "
        << info.video_info.bit_rate << ", "
        << quote(connection_, info.audio_info.codec_name) << ", "
        << info.audio_info.sample_rate << ", "
        << info.audio_info.channels << ", "
        << info.audio_info.bit_rate << ", "
        << quote(connection_, title) << ", "
        << quote(connection_, info.tags) << ")";

    if (!execute(connection_, sql.str(), last_error_, "添加失败")) {
        std::cerr << "[Database] " << last_error_ << std::endl;
        return -1;
    }

    int id = static_cast<int>(mysql_insert_id(connection_));
    std::cout << "[Database] 添加媒体: " << info.file_name << " (ID=" << id << ")" << std::endl;
    return id;
}

// ============================================================
// 更新媒体文件
// ============================================================
bool MediaDatabase::updateMedia(int id, const MediaInfo& info)
{
    if (!connection_) return false;

    std::ostringstream sql;
    sql << "UPDATE media_library SET title=" << quote(connection_, info.title)
        << ", tags=" << quote(connection_, info.tags)
        << ", is_favorite=" << (info.is_favorite ? 1 : 0)
        << " WHERE id=" << id;

    if (!execute(connection_, sql.str(), last_error_, "更新失败")) {
        return false;
    }

    return mysql_affected_rows(connection_) > 0;
}

// ============================================================
// 删除媒体文件
// ============================================================
bool MediaDatabase::deleteMedia(int id)
{
    if (!connection_) return false;

    std::string sql = "DELETE FROM media_library WHERE id=" + std::to_string(id);
    if (!execute(connection_, sql, last_error_, "删除失败")) {
        return false;
    }

    return mysql_affected_rows(connection_) > 0;
}

// ============================================================
// 根据ID查询
// ============================================================
bool MediaDatabase::getMediaById(int id, MediaInfo& info)
{
    if (!connection_) return false;

    std::string sql = "SELECT * FROM media_library WHERE id=" + std::to_string(id);
    MYSQL_RES* result = query(connection_, sql, last_error_, "查询ID失败");
    if (!result) return false;

    bool found = false;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row) {
        readMediaFromResultSet(row, mysql_fetch_lengths(result), info);
        found = true;
    }

    mysql_free_result(result);
    return found;
}

// ============================================================
// 根据路径查询媒体文件
// ============================================================
bool MediaDatabase::getMediaByPath(const std::string& path, MediaInfo& info)
{
    if (!connection_) return false;

    std::string sql = "SELECT * FROM media_library WHERE file_path = " + quote(connection_, path);
    MYSQL_RES* result = query(connection_, sql, last_error_, "查询路径失败");
    if (!result) return false;

    bool found = false;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row) {
        readMediaFromResultSet(row, mysql_fetch_lengths(result), info);
        found = true;
    }

    mysql_free_result(result);
    return found;
}

// ============================================================
// 获取所有媒体文件列表
// ============================================================
bool MediaDatabase::getAllMedia(std::vector<MediaInfo>& media_list)
{
    if (!connection_) return false;

    MYSQL_RES* result = query(
        connection_,
        "SELECT * FROM media_library ORDER BY added_time DESC",
        last_error_,
        "查询失败"
    );
    if (!result) return false;

    media_list.clear();
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        MediaInfo info;
        readMediaFromResultSet(row, mysql_fetch_lengths(result), info);
        media_list.push_back(info);
    }

    mysql_free_result(result);
    return true;
}

// ============================================================
// 搜索媒体
// ============================================================
bool MediaDatabase::searchMedia(const std::string& keyword, std::vector<MediaInfo>& results)
{
    if (!connection_) return false;

    std::string pattern = "%" + keyword + "%";
    std::ostringstream sql;
    sql << "SELECT * FROM media_library WHERE "
        << "file_name LIKE " << quote(connection_, pattern)
        << " OR title LIKE " << quote(connection_, pattern)
        << " OR tags LIKE " << quote(connection_, pattern)
        << " ORDER BY added_time DESC";

    MYSQL_RES* result = query(connection_, sql.str(), last_error_, "搜索失败");
    if (!result) return false;

    results.clear();
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        MediaInfo info;
        readMediaFromResultSet(row, mysql_fetch_lengths(result), info);
        results.push_back(info);
    }

    mysql_free_result(result);
    return true;
}

// ============================================================
// 记录播放历史
// ============================================================
bool MediaDatabase::recordPlayHistory(int media_id)
{
    if (!connection_) return false;

    std::string insert_sql = "INSERT INTO play_history (media_id) VALUES (" + std::to_string(media_id) + ")";
    if (!execute(connection_, insert_sql, last_error_, "记录播放历史失败")) {
        return false;
    }

    std::string update_sql = "UPDATE media_library SET play_count = play_count + 1, last_play_time = NOW() WHERE id = " + std::to_string(media_id);
    return execute(connection_, update_sql, last_error_, "记录播放历史失败");
}

// ============================================================
// 收藏操作
// ============================================================
bool MediaDatabase::setFavorite(int id, bool favorite)
{
    if (!connection_) return false;

    std::ostringstream sql;
    sql << "UPDATE media_library SET is_favorite = " << (favorite ? 1 : 0)
        << " WHERE id = " << id;

    if (!execute(connection_, sql.str(), last_error_, "收藏操作失败")) {
        return false;
    }

    return mysql_affected_rows(connection_) > 0;
}

bool MediaDatabase::getFavorites(std::vector<MediaInfo>& results)
{
    if (!connection_) return false;

    MYSQL_RES* result = query(
        connection_,
        "SELECT * FROM media_library WHERE is_favorite = 1 ORDER BY title",
        last_error_,
        "查询收藏失败"
    );
    if (!result) return false;

    results.clear();
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        MediaInfo info;
        readMediaFromResultSet(row, mysql_fetch_lengths(result), info);
        results.push_back(info);
    }

    mysql_free_result(result);
    return true;
}

bool MediaDatabase::getRecentPlayed(std::vector<MediaInfo>& results, int limit)
{
    if (!connection_) return false;

    if (limit <= 0) {
        limit = 20;
    }

    std::ostringstream sql;
    sql << "SELECT m.* FROM media_library m "
        << "INNER JOIN play_history p ON m.id = p.media_id "
        << "GROUP BY m.id ORDER BY MAX(p.play_time) DESC LIMIT " << limit;

    MYSQL_RES* result = query(connection_, sql.str(), last_error_, "查询最近播放失败");
    if (!result) return false;

    results.clear();
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr) {
        MediaInfo info;
        readMediaFromResultSet(row, mysql_fetch_lengths(result), info);
        results.push_back(info);
    }

    mysql_free_result(result);
    return true;
}

// ============================================================
// 从ResultSet读取MediaInfo
// ============================================================
void MediaDatabase::readMediaFromResultSet(MYSQL_ROW row, unsigned long* lengths, MediaInfo& info)
{
    info.db_id = toInt(row, 0);
    info.file_path = field(row, lengths, 1);
    info.file_name = field(row, lengths, 2);
    info.format_name = field(row, lengths, 3);
    info.duration = toDouble(row, 4);
    info.file_size = toInt64(row, 5);

    info.video_info.width = toInt(row, 6);
    info.video_info.height = toInt(row, 7);
    info.video_info.codec_name = field(row, lengths, 8);
    info.video_info.frame_rate = toDouble(row, 9);
    info.video_info.bit_rate = toInt(row, 10);

    info.audio_info.codec_name = field(row, lengths, 11);
    info.audio_info.sample_rate = toInt(row, 12);
    info.audio_info.channels = toInt(row, 13);
    info.audio_info.bit_rate = toInt(row, 14);

    info.title = field(row, lengths, 15);
    info.tags = field(row, lengths, 16);
    info.is_favorite = toInt(row, 17) != 0;
    info.play_count = toInt(row, 18);

    info.added_time = field(row, lengths, 19);
    info.last_play_time = field(row, lengths, 20);

    info.has_video = (info.video_info.width > 0);
    info.has_audio = (info.audio_info.sample_rate > 0);
}
