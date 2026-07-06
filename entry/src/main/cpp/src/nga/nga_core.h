/**
 * NGA 6-DoF 核心算法头文件
 * 移植自 nga.py — 特征点(SIFT) + 稠密光流(Farneback) 双引擎架构
 */

#ifndef NGA_CORE_H
#define NGA_CORE_H

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
// calib3d removed — using direct keypoint displacement instead of findHomography
#include <opencv2/video.hpp>
#include <string>
#include <vector>

// ============================================================================
// 数据结构定义
// ============================================================================

/**
 * @brief NGA 6自由度分析结果
 */
struct Nga6DofResult {
    double dX_px = 0.0;          // 水平像素偏移 (正=当前偏右)
    double dY_px = 0.0;          // 垂直像素偏移 (正=当前偏下)
    double dRoll_deg = 0.0;      // 旋转角度 (度)
    double dPitch_warp = 0.0;    // 俯仰透视分量 (×1000)
    double dYaw_warp = 0.0;      // 偏航透视分量 (×1000)
    double scale_factor = 1.0;   // 缩放因子 (>1 = 当前更近/更大)
    double perspective_ratio = 1.0; // 透视比
    bool is_pure_zoom = false;   // 是否纯变焦
};

/**
 * @brief NGA 处理结果 (含引导指令和可视化)
 */
struct NgaResult {
    bool success = false;
    std::string alignment_method;     // "SIFT" 或 "DEPTH_FALLBACK"
    std::string error_reason;         // 失败原因
    Nga6DofResult dof;                // 6DOF参数
    std::vector<std::string> commands; // 用户引导指令
    bool is_aligned = false;          // 是否已对齐 (可触发自动抓拍)
    bool tracking_lost = false;       // 特征点/光流均不可靠，跟踪丢失
    int match_count = 0;              // SIFT 内点数（诊断用）
};

// ============================================================================
// NGA 引擎类
// ============================================================================

class NgaEngine {
public:
    NgaEngine();
    ~NgaEngine();

    /**
     * @brief 初始化引擎 (创建SIFT检测器等)
     * @param width  预期图像宽度
     * @param height 预期图像高度
     * @return 是否初始化成功
     */
    bool init(int width, int height);

    /**
     * @brief 设置参考帧 (目标画面)
     * @param rgbaData RGBA格式像素数据
     * @param width    图像宽度
     * @param height   图像高度
     * @return 是否设置成功
     */
    bool setReferenceFrame(const uint8_t* rgbaData, int width, int height);

    /**
     * @brief 处理当前帧，计算与参考帧的6DOF差异
     * @param rgbaData RGBA格式像素数据
     * @param width    图像宽度
     * @param height   图像高度
     * @return 处理结果 (含引导指令和对齐判定)
     */
    NgaResult processFrame(const uint8_t* rgbaData, int width, int height);

    /**
     * @brief 销毁引擎，释放资源
     */
    void destroy();

    /**
     * @brief 检查是否已设置参考帧
     */
    bool hasReference() const { return m_hasReference; }

    /**
     * @brief 获取对齐判定阈值
     */
    double getAlignThresholdDx() const { return m_alignThresholdDx; }
    double getAlignThresholdDy() const { return m_alignThresholdDy; }
    double getAlignThresholdRoll() const { return m_alignThresholdRoll; }
    double getAlignThresholdScale() const { return m_alignThresholdScale; }

    /**
     * @brief 设置对齐判定阈值
     */
    void setAlignThresholds(double dx, double dy, double roll, double scale);

private:
    // ---- 核心算法 (对应 nga.py 中的函数) ----

    /**
     * @brief 高精特征配准 (SIFT + Homography)
     * 尝试一：对应 nga.py 第70-115行
     */
    NgaResult trySiftAlignment(const cv::Mat& grayRef, const cv::Mat& grayCur);

    /**
     * @brief 稠密几何兜底方案 (Farneback光流)
     * 尝试二：对应 nga.py fallback_dense_geometry_alignment()
     */
    Nga6DofResult fallbackDenseGeometry(const cv::Mat& grayRef, const cv::Mat& grayCur);

    /**
     * @brief 计算 dHash（差异哈希）用于场景结构变化检测
     */
    uint64_t computeDHash(const cv::Mat& gray);

    /**
     * @brief 将灰度图转换为梯度幅值图（只保留边缘结构，消除纹理/风格差异）
     * AI 风格化图与实拍图在梯度域具有相似的边缘结构
     */
    cv::Mat computeStructuralImage(const cv::Mat& gray);

    /**
     * @brief 根据6DOF参数生成用户引导指令
     * 对应 nga.py 第132-144行
     */
    std::vector<std::string> generateGuidanceCommands(const Nga6DofResult& dof);

    /**
     * @brief 判定当前帧是否与参考帧对齐
     * 所有6DOF参数都在阈值内 → 对齐
     */
    bool checkAlignment(const Nga6DofResult& dof);

    /**
     * @brief 将RGBA数据转换为灰度cv::Mat
     */
    cv::Mat rgbaToGray(const uint8_t* rgbaData, int width, int height);

    /**
     * @brief 计算向量的中位数
     */
    static double median(const std::vector<double>& data);

    // ---- 成员变量 ----

    cv::Ptr<cv::SIFT> m_sift;           // SIFT特征检测器
    cv::Mat m_refGray;                  // 参考帧灰度图
    uint64_t m_refHash = 0;             // 参考帧 dHash
    cv::Mat m_refDescriptors;           // 参考帧SIFT描述子
    std::vector<cv::KeyPoint> m_refKeypoints; // 参考帧关键点

    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
    bool m_hasReference = false;

    // 对齐判定阈值 (与Python版一致)
    double m_alignThresholdDx = 12.0;    // 水平像素阈值
    double m_alignThresholdDy = 12.0;    // 垂直像素阈值
    double m_alignThresholdRoll = 0.6;   // 旋转角度阈值 (度)
    double m_alignThresholdScale = 0.03; // 缩放因子偏差阈值

    // SIFT参数
    int m_siftFeatures = 4000;           // SIFT特征点数量上限
    double m_ratioTestThreshold = 0.60;  // Lowe's ratio test 阈值（更严格）
    int m_minGoodMatches = 15;           // 最少好匹配数
    double m_ransacThreshold = 3.0;      // RANSAC 重投影误差阈值
    // 指令生成阈值 (与Python版一致)
    double m_guidanceDxThreshold = 12.0;
    double m_guidanceDyThreshold = 12.0;
    double m_guidanceRollThreshold = 0.6;
    double m_guidanceScaleThreshold = 0.03;
};

// 全局引擎实例管理 (实现在 nga_core.cpp)
extern "C" {
    NgaEngine* GetNgaEngine();
    void ReleaseNgaEngine();
}

#endif // NGA_CORE_H
