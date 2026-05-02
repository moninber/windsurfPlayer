-- ============================================================
-- MediaStudio 数据库初始化脚本
-- 
-- 使用方法：
-- 1. 确保MySQL 8.0服务正在运行
-- 2. 使用root用户登录MySQL：
--    mysql -u root -p < init_database.sql
-- 3. 或在MySQL客户端中执行：
--    source E:/vs2026-code/windsurftest/CascadeProjects/windsurf-project/sql/init_database.sql
-- ============================================================

-- 创建数据库（如果不存在）
CREATE DATABASE IF NOT EXISTS media_center
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

-- 使用该数据库
USE media_center;

-- ============================================================
-- 媒体库主表
-- 存储媒体文件的基本信息和元数据
-- ============================================================
CREATE TABLE IF NOT EXISTS media_library (
    id INT AUTO_INCREMENT PRIMARY KEY,
    file_path VARCHAR(500) NOT NULL UNIQUE COMMENT '文件完整路径',
    file_name VARCHAR(200) COMMENT '文件名',
    format VARCHAR(50) COMMENT '容器格式(mp4/mkv/avi)',
    duration DOUBLE DEFAULT 0 COMMENT '时长(秒)',
    file_size BIGINT DEFAULT 0 COMMENT '文件大小(字节)',
    
    -- 视频信息
    video_width INT DEFAULT 0 COMMENT '视频宽度',
    video_height INT DEFAULT 0 COMMENT '视频高度',
    video_codec VARCHAR(50) COMMENT '视频编码器(h264/hevc)',
    video_frame_rate DOUBLE DEFAULT 0 COMMENT '帧率(fps)',
    video_bit_rate INT DEFAULT 0 COMMENT '视频码率(bps)',
    
    -- 音频信息
    audio_codec VARCHAR(50) COMMENT '音频编码器(aac/mp3)',
    audio_sample_rate INT DEFAULT 0 COMMENT '采样率(Hz)',
    audio_channels INT DEFAULT 0 COMMENT '声道数',
    audio_bit_rate INT DEFAULT 0 COMMENT '音频码率(bps)',
    
    -- 用户自定义信息
    title VARCHAR(200) COMMENT '自定义标题',
    tags VARCHAR(500) COMMENT '标签(逗号分隔)',
    is_favorite TINYINT DEFAULT 0 COMMENT '是否收藏(0/1)',
    play_count INT DEFAULT 0 COMMENT '播放次数',
    added_time DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '添加时间',
    last_play_time DATETIME COMMENT '最后播放时间'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='媒体库主表';

-- ============================================================
-- 播放历史表
-- 记录每次播放的时间
-- ============================================================
CREATE TABLE IF NOT EXISTS play_history (
    id INT AUTO_INCREMENT PRIMARY KEY,
    media_id INT NOT NULL COMMENT '媒体ID',
    play_time DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '播放时间',
    FOREIGN KEY (media_id) REFERENCES media_library(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='播放历史表';

-- ============================================================
-- 播放列表表
-- ============================================================
CREATE TABLE IF NOT EXISTS playlists (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL COMMENT '列表名称',
    created_time DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='播放列表表';

-- ============================================================
-- 播放列表项表（播放列表与媒体文件的多对多关系）
-- ============================================================
CREATE TABLE IF NOT EXISTS playlist_items (
    id INT AUTO_INCREMENT PRIMARY KEY,
    playlist_id INT NOT NULL COMMENT '播放列表ID',
    media_id INT NOT NULL COMMENT '媒体ID',
    position INT DEFAULT 0 COMMENT '排序位置',
    FOREIGN KEY (playlist_id) REFERENCES playlists(id) ON DELETE CASCADE,
    FOREIGN KEY (media_id) REFERENCES media_library(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='播放列表项表';

-- ============================================================
-- 创建索引（加速查询）
-- ============================================================
CREATE INDEX idx_file_name ON media_library(file_name);
CREATE INDEX idx_is_favorite ON media_library(is_favorite);
CREATE INDEX idx_last_play ON media_library(last_play_time);
CREATE INDEX idx_play_time ON play_history(play_time);

-- 插入示例播放列表
INSERT IGNORE INTO playlists (name) VALUES ('默认播放列表');
INSERT IGNORE INTO playlists (name) VALUES ('收藏夹');

-- 完成
SELECT 'MediaStudio 数据库初始化完成！' AS message;
