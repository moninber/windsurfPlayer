/**
 * @file MediaDatabase.cpp
 * @brief 媒体数据库实现
 */

#include "MediaDatabase.h"
#include <iostream>

MediaDatabase::MediaDatabase()
    : driver_(nullptr)
    , connection_(nullptr)
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
    try {
        // 获取MySQL驱动实例（全局单例，由Connector/C++管理生命周期）
        driver_ = sql::mysql::get_mysql_driver_instance();

        // 构建连接URL：tcp://host:port
        std::string url = "tcp://" + host + ":" + std::to_string(port);

        // 建立连接
        connection_ = driver_->connect(url, user, password);

        if (!connection_->isValid()) {
            last_error_ = "数据库连接无效";
            delete connection_;
            connection_ = nullptr;
            return false;
        }

        // 选择数据库
        connection_->setSchema(database);
        database_name_ = database;

        std::cout << "[Database] 连接成功: " << url << " / " << database << std::endl;
        return true;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("MySQL错误: ") + e.what();
        std::cerr << "[Database] " << last_error_ << std::endl;
        if (connection_) {
            delete connection_;
            connection_ = nullptr;
        }
        return false;
    }
}

void MediaDatabase::disconnect()
{
    if (connection_) {
        try {
            connection_->close();
        }
        catch (...) {}
        delete connection_;
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

    try {
        sql::Statement* stmt = connection_->createStatement();

        // --------------------------------------------------------
        // 媒体库主表
        /** 
        * 字段说明：
        * - id: 自增主键
        * - file_path: 文件完整路径（唯一约束，防止重复添加）
        * - file_name: 文件名
        * - format: 容器格式
        * - duration: 时长（秒）
        * - file_size: 文件大小（字节）
        * - video_width/height: 视频分辨率
        * - video_codec: 视频编码器
        * - video_frame_rate: 帧率
        * - video_bit_rate: 视频码率
        * - audio_codec: 音频编码器
        * - audio_sample_rate: 采样率
        * - audio_channels: 声道数
        * - audio_bit_rate: 音频码率
        * - title: 自定义标题
        * - tags: 标签（逗号分隔）
        * - is_favorite: 是否收藏
        * - play_count: 播放次数
        * - added_time: 添加时间
        * - last_play_time: 最后播放时间*/
        // --------------------------------------------------------
        stmt->execute(R"(
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
        )");

        // --------------------------------------------------------
        // 播放历史表
        // --------------------------------------------------------
        stmt->execute(R"(
            CREATE TABLE IF NOT EXISTS play_history (
                id INT AUTO_INCREMENT PRIMARY KEY,
                media_id INT NOT NULL,
                play_time DATETIME DEFAULT CURRENT_TIMESTAMP,
                FOREIGN KEY (media_id) REFERENCES media_library(id) ON DELETE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");

        // --------------------------------------------------------
        // 播放列表表
        // --------------------------------------------------------
        stmt->execute(R"(
            CREATE TABLE IF NOT EXISTS playlists (
                id INT AUTO_INCREMENT PRIMARY KEY,
                name VARCHAR(100) NOT NULL,
                created_time DATETIME DEFAULT CURRENT_TIMESTAMP
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");

        // 播放列表项表（多对多关系）
        stmt->execute(R"(
            CREATE TABLE IF NOT EXISTS playlist_items (
                id INT AUTO_INCREMENT PRIMARY KEY,
                playlist_id INT NOT NULL,
                media_id INT NOT NULL,
                position INT DEFAULT 0,
                FOREIGN KEY (playlist_id) REFERENCES playlists(id) ON DELETE CASCADE,
                FOREIGN KEY (media_id) REFERENCES media_library(id) ON DELETE CASCADE
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4
        )");

        delete stmt;
        std::cout << "[Database] 表初始化成功" << std::endl;
        return true;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("建表失败: ") + e.what();
        std::cerr << "[Database] " << last_error_ << std::endl;
        return false;
    }
}

// ============================================================
// 添加媒体文件
// ============================================================
int MediaDatabase::addMedia(const MediaInfo& info)
{
    if (!connection_) return -1;

    try {
        // 使用PreparedStatement防止SQL注入
        // ? 是参数占位符，后续通过setXXX方法设置值
        sql::PreparedStatement* pstmt = connection_->prepareStatement(
            "INSERT INTO media_library "
            "(file_path, file_name, format, duration, file_size, "
            "video_width, video_height, video_codec, video_frame_rate, video_bit_rate, "
            "audio_codec, audio_sample_rate, audio_channels, audio_bit_rate, "
            "title, tags) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        );

        // 设置参数（索引从1开始）
        pstmt->setString(1, info.file_path);
        pstmt->setString(2, info.file_name);
        pstmt->setString(3, info.format_name);
        pstmt->setDouble(4, info.duration);
        pstmt->setInt64(5, info.file_size);
        pstmt->setInt(6, info.video_info.width);
        pstmt->setInt(7, info.video_info.height);
        pstmt->setString(8, info.video_info.codec_name);
        pstmt->setDouble(9, info.video_info.frame_rate);
        pstmt->setInt(10, info.video_info.bit_rate);
        pstmt->setString(11, info.audio_info.codec_name);
        pstmt->setInt(12, info.audio_info.sample_rate);
        pstmt->setInt(13, info.audio_info.channels);
        pstmt->setInt(14, info.audio_info.bit_rate);
        pstmt->setString(15, info.title.empty() ? info.file_name : info.title);
        pstmt->setString(16, info.tags);

        pstmt->executeUpdate();
        delete pstmt;

        // 获取自增ID
        sql::Statement* stmt = connection_->createStatement();
        sql::ResultSet* rs = stmt->executeQuery("SELECT LAST_INSERT_ID()");
        int id = -1;
        if (rs->next()) {
            id = rs->getInt(1);
        }
        delete rs;
        delete stmt;

        std::cout << "[Database] 添加媒体: " << info.file_name << " (ID=" << id << ")" << std::endl;
        return id;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("添加失败: ") + e.what();
        std::cerr << "[Database] " << last_error_ << std::endl;
        return -1;
    }
}

// ============================================================
// 更新媒体文件
// ============================================================
bool MediaDatabase::updateMedia(int id, const MediaInfo& info)
{
    if (!connection_) return false;

    try {
        sql::PreparedStatement* pstmt = connection_->prepareStatement(
            "UPDATE media_library SET title=?, tags=?, is_favorite=? WHERE id=?"
        );
        pstmt->setString(1, info.title);
        pstmt->setString(2, info.tags);
        pstmt->setInt(3, info.is_favorite ? 1 : 0);
        pstmt->setInt(4, id);

        int rows = pstmt->executeUpdate();
        delete pstmt;
        return rows > 0;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("更新失败: ") + e.what();
        return false;
    }
}

// ============================================================
// 删除媒体文件
// ============================================================
bool MediaDatabase::deleteMedia(int id)
{
    if (!connection_) return false;

    try {
        sql::PreparedStatement* pstmt = connection_->prepareStatement(
            "DELETE FROM media_library WHERE id=?"
        );
        pstmt->setInt(1, id);
        int rows = pstmt->executeUpdate();
        delete pstmt;
        return rows > 0;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("删除失败: ") + e.what();
        return false;
    }
}

// ============================================================
// 根据ID查询
// ============================================================
bool MediaDatabase::getMediaById(int id, MediaInfo& info)
{
    if (!connection_) return false;

    try {
        sql::PreparedStatement* pstmt = connection_->prepareStatement(
            "SELECT * FROM media_library WHERE id=?"
        );
        pstmt->setInt(1, id);
        sql::ResultSet* rs = pstmt->executeQuery();

        bool found = false;
        if (rs->next()) {
            readMediaFromResultSet(rs, info);
            found = true;
        }

        delete rs;
        delete pstmt;
        return found;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("查询失败: ") + e.what();
        return false;
    }
}

// ============================================================
// 获取所有媒体
// ============================================================
bool MediaDatabase::getAllMedia(std::vector<MediaInfo>& media_list)
{
    if (!connection_) return false;

    try {
        sql::Statement* stmt = connection_->createStatement();
        sql::ResultSet* rs = stmt->executeQuery(
            "SELECT * FROM media_library ORDER BY added_time DESC"
        );

        media_list.clear();
        while (rs->next()) {
            MediaInfo info;
            readMediaFromResultSet(rs, info);
            media_list.push_back(info);
        }

        delete rs;
        delete stmt;
        return true;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("查询失败: ") + e.what();
        return false;
    }
}

// ============================================================
// 搜索媒体
// ============================================================
bool MediaDatabase::searchMedia(const std::string& keyword, std::vector<MediaInfo>& results)
{
    if (!connection_) return false;

    try {
        // 使用LIKE进行模糊搜索
        // %keyword% 匹配包含关键词的任意字符串
        sql::PreparedStatement* pstmt = connection_->prepareStatement(
            "SELECT * FROM media_library WHERE "
            "file_name LIKE ? OR title LIKE ? OR tags LIKE ? "
            "ORDER BY added_time DESC"
        );

        std::string pattern = "%" + keyword + "%";
        pstmt->setString(1, pattern);
        pstmt->setString(2, pattern);
        pstmt->setString(3, pattern);

        sql::ResultSet* rs = pstmt->executeQuery();
        results.clear();
        while (rs->next()) {
            MediaInfo info;
            readMediaFromResultSet(rs, info);
            results.push_back(info);
        }

        delete rs;
        delete pstmt;
        return true;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("搜索失败: ") + e.what();
        return false;
    }
}

// ============================================================
// 记录播放历史
// ============================================================
bool MediaDatabase::recordPlayHistory(int media_id)
{
    if (!connection_) return false;

    try {
        // 插入播放历史记录
        sql::PreparedStatement* pstmt = connection_->prepareStatement(
            "INSERT INTO play_history (media_id) VALUES (?)"
        );
        pstmt->setInt(1, media_id);
        pstmt->executeUpdate();
        delete pstmt;

        // 更新媒体文件的播放次数和最后播放时间
        sql::PreparedStatement* pstmt2 = connection_->prepareStatement(
            "UPDATE media_library SET play_count = play_count + 1, "
            "last_play_time = NOW() WHERE id = ?"
        );
        pstmt2->setInt(1, media_id);
        pstmt2->executeUpdate();
        delete pstmt2;

        return true;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("记录播放历史失败: ") + e.what();
        return false;
    }
}

// ============================================================
// 收藏操作
// ============================================================
bool MediaDatabase::setFavorite(int id, bool favorite)
{
    if (!connection_) return false;

    try {
        sql::PreparedStatement* pstmt = connection_->prepareStatement(
            "UPDATE media_library SET is_favorite = ? WHERE id = ?"
        );
        pstmt->setInt(1, favorite ? 1 : 0);
        pstmt->setInt(2, id);
        int rows = pstmt->executeUpdate();
        delete pstmt;
        return rows > 0;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("收藏操作失败: ") + e.what();
        return false;
    }
}

bool MediaDatabase::getFavorites(std::vector<MediaInfo>& results)
{
    if (!connection_) return false;

    try {
        sql::Statement* stmt = connection_->createStatement();
        sql::ResultSet* rs = stmt->executeQuery(
            "SELECT * FROM media_library WHERE is_favorite = 1 ORDER BY title"
        );

        results.clear();
        while (rs->next()) {
            MediaInfo info;
            readMediaFromResultSet(rs, info);
            results.push_back(info);
        }

        delete rs;
        delete stmt;
        return true;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("查询收藏失败: ") + e.what();
        return false;
    }
}

bool MediaDatabase::getRecentPlayed(std::vector<MediaInfo>& results, int limit)
{
    if (!connection_) return false;

    try {
        sql::PreparedStatement* pstmt = connection_->prepareStatement(
            "SELECT m.* FROM media_library m "
            "INNER JOIN play_history p ON m.id = p.media_id "
            "GROUP BY m.id ORDER BY MAX(p.play_time) DESC LIMIT ?"
        );
        pstmt->setInt(1, limit);

        sql::ResultSet* rs = pstmt->executeQuery();
        results.clear();
        while (rs->next()) {
            MediaInfo info;
            readMediaFromResultSet(rs, info);
            results.push_back(info);
        }

        delete rs;
        delete pstmt;
        return true;
    }
    catch (const sql::SQLException& e) {
        last_error_ = std::string("查询最近播放失败: ") + e.what();
        return false;
    }
}

// ============================================================
// 从ResultSet读取MediaInfo
// ============================================================
void MediaDatabase::readMediaFromResultSet(sql::ResultSet* rs, MediaInfo& info)
{
    info.db_id = rs->getInt("id");
    info.file_path = rs->getString("file_path");
    info.file_name = rs->getString("file_name");
    info.format_name = rs->getString("format");
    info.duration = rs->getDouble("duration");
    info.file_size = rs->getInt64("file_size");

    info.video_info.width = rs->getInt("video_width");
    info.video_info.height = rs->getInt("video_height");
    info.video_info.codec_name = rs->getString("video_codec");
    info.video_info.frame_rate = rs->getDouble("video_frame_rate");
    info.video_info.bit_rate = rs->getInt("video_bit_rate");

    info.audio_info.codec_name = rs->getString("audio_codec");
    info.audio_info.sample_rate = rs->getInt("audio_sample_rate");
    info.audio_info.channels = rs->getInt("audio_channels");
    info.audio_info.bit_rate = rs->getInt("audio_bit_rate");

    info.title = rs->getString("title");
    info.tags = rs->getString("tags");
    info.is_favorite = rs->getInt("is_favorite") != 0;
    info.play_count = rs->getInt("play_count");

    // 处理可能为NULL的字段
    sql::SQLString added = rs->getString("added_time");
    info.added_time = added.c_str();
    if (!rs->wasNull()) {
        sql::SQLString last = rs->getString("last_play_time");
        info.last_play_time = last.c_str();
    }

    info.has_video = (info.video_info.width > 0);
    info.has_audio = (info.audio_info.sample_rate > 0);
}
