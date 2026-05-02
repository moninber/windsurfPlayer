/**
 * @file MediaDatabase.h
 * @brief 媒体数据库 - 基于MySQL的媒体库管理
 * 
 * 本类使用MySQL Connector/C++实现媒体文件的数据库存储与管理：
 * 
 * MySQL Connector/C++ 使用流程：
 * 1. sql::mysql::get_mysql_driver_instance() → 获取驱动实例
 * 2. driver->connect()                       → 建立数据库连接
 * 3. con->createStatement()                  → 创建SQL语句对象
 * 4. stmt->execute() / stmt->executeQuery()  → 执行SQL
 * 5. ResultSet遍历                            → 处理查询结果
 * 
 * 数据库表设计：
 * - media_library：主表，存储媒体文件信息和元数据
 * - play_history：播放历史记录表
 * - playlists：播放列表表
 * 
 * 学习要点：
 * - MySQL Connector/C++ API
 * - SQL语句编写（CREATE, INSERT, SELECT, UPDATE, DELETE）
 * - Prepared Statement（参数化查询，防止SQL注入）
 * - 数据库连接管理
 * - 事务处理基础
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include "MediaInfo.h"

// MySQL Connector/C++ 头文件
// 包含路径需要配置到项目属性中
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/statement.h>
#include <cppconn/resultset.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/exception.h>

class MediaDatabase {
public:
    MediaDatabase();
    ~MediaDatabase();

    /**
     * @brief 连接到MySQL数据库
     * @param host 主机地址（如"127.0.0.1"）
     * @param user 用户名（如"root"）
     * @param password 密码
     * @param database 数据库名（如"media_center"）
     * @param port 端口号（默认3306）
     * @return true=连接成功
     */
    bool connect(const std::string& host, const std::string& user,
                 const std::string& password, const std::string& database,
                 int port = 3306);

    /** @brief 断开连接 */
    void disconnect();

    /** @brief 是否已连接 */
    bool isConnected() const { return connection_ != nullptr; }

    /**
     * @brief 初始化数据库表结构
     * 创建所需的表（如果不存在）
     * @return true=成功
     */
    bool initTables();

    /**
     * @brief 添加媒体文件到数据库
     * @param info 媒体文件信息
     * @return 数据库记录ID，-1=失败
     */
    int addMedia(const MediaInfo& info);

    /**
     * @brief 更新媒体文件信息
     * @param id 记录ID
     * @param info 新的媒体信息
     * @return true=成功
     */
    bool updateMedia(int id, const MediaInfo& info);

    /**
     * @brief 删除媒体文件记录
     * @param id 记录ID
     * @return true=成功
     */
    bool deleteMedia(int id);

    /**
     * @brief 根据ID查询媒体文件
     * @param id 记录ID
     * @param info 输出媒体信息
     * @return true=找到记录
     */
    bool getMediaById(int id, MediaInfo& info);

    /**
     * @brief 根据路径查询媒体文件
     * @param path 文件完整路径
     * @param info 输出媒体信息
     * @return true=找到记录
     */
    bool getMediaByPath(const std::string& path, MediaInfo& info);

    /**
     * @brief 获取所有媒体文件列表
     * @param media_list 输出列表
     * @return true=成功
     */
    bool getAllMedia(std::vector<MediaInfo>& media_list);

    /**
     * @brief 按标题搜索媒体文件
     * @param keyword 搜索关键词
     * @param results 输出结果列表
     * @return true=成功
     */
    bool searchMedia(const std::string& keyword, std::vector<MediaInfo>& results);

    /**
     * @brief 记录播放历史
     * @param media_id 媒体ID
     * @return true=成功
     */
    bool recordPlayHistory(int media_id);

    /**
     * @brief 设置/取消收藏
     * @param id 媒体ID
     * @param favorite 是否收藏
     * @return true=成功
     */
    bool setFavorite(int id, bool favorite);

    /**
     * @brief 获取收藏列表
     * @param results 输出结果
     * @return true=成功
     */
    bool getFavorites(std::vector<MediaInfo>& results);

    /**
     * @brief 获取最近播放列表
     * @param results 输出结果
     * @param limit 数量限制
     * @return true=成功
     */
    bool getRecentPlayed(std::vector<MediaInfo>& results, int limit = 20);

    /** @brief 获取最后的错误信息 */
    const std::string& getLastError() const { return last_error_; }

private:
    /**
     * @brief 从ResultSet中读取MediaInfo
     * @param rs 结果集
     * @param info 输出信息
     */
    void readMediaFromResultSet(sql::ResultSet* rs, MediaInfo& info);

    sql::mysql::MySQL_Driver* driver_;     // MySQL驱动（单例，不需要释放）
    sql::Connection* connection_;           // 数据库连接
    std::string last_error_;                // 最后错误信息
    std::string database_name_;             // 当前数据库名
};
