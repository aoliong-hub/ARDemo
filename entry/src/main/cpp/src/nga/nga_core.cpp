/**
 * NGA 6-DoF 核心算法实现
 * 完整移植自 nga.py
 * 特征点(SIFT) + 稠密光流(Farneback) 双引擎架构
 */

#include "nga_core.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>

// ============================================================================
// 构造函数 / 析构函数
// ============================================================================

NgaEngine::NgaEngine() = default;

NgaEngine::~NgaEngine() {
    destroy();
}

// ============================================================================
// 公共接口
// ============================================================================

bool NgaEngine::init(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    m_width = width;
    m_height = height;

    // 创建 SIFT 检测器 (对应Python: cv2.SIFT_create(nfeatures=2000))
    m_sift = cv::SIFT::create(m_siftFeatures);
    if (m_sift == nullptr) {
        return false;
    }

    m_initialized = true;
    return true;
}

bool NgaEngine::setReferenceFrame(const uint8_t* rgbaData, int width, int height) {
    if (!m_initialized || rgbaData == nullptr) {
        return false;
    }

    // 调整内部缓存尺寸
    if (width != m_width || height != m_height) {
        m_width = width;
        m_height = height;
    }

    // 转换为灰度图并降采样
    m_refGray = rgbaToGray(rgbaData, width, height);
    const int kMaxDim = 640;
    if (std::max(m_refGray.cols, m_refGray.rows) > kMaxDim) {
        double s = (double)kMaxDim / std::max(m_refGray.cols, m_refGray.rows);
        cv::resize(m_refGray, m_refGray, cv::Size((int)(m_refGray.cols * s), (int)(m_refGray.rows * s)));
    }
    m_width = m_refGray.cols;
    m_height = m_refGray.rows;

    // 缓存 dHash（在原始灰度图上计算，用于场景变化检测）
    m_refHash = computeDHash(m_refGray);

    // 计算梯度幅值图（边缘结构域），在此之上提取 SIFT 特征
    // AI 风格化图与实拍图在梯度域共享相似的边缘结构
    cv::Mat refStruct = computeStructuralImage(m_refGray);
    m_sift->detectAndCompute(refStruct, cv::noArray(), m_refKeypoints, m_refDescriptors);

    if (m_refDescriptors.empty() || m_refKeypoints.size() < 15) {
        // 参考帧特征点不足，但仍可降级使用光流兜底
        // 保留灰度图用于光流计算
    }

    m_hasReference = true;
    return true;
}

NgaResult NgaEngine::processFrame(const uint8_t* rgbaData, int width, int height) {
    NgaResult result;

    if (!m_initialized || !m_hasReference) {
        result.success = false;
        result.error_reason = "引擎未初始化或未设置参考帧";
        return result;
    }

    if (rgbaData == nullptr) {
        result.success = false;
        result.error_reason = "输入帧数据为空";
        return result;
    }

    // 转换当前帧为灰度图，并降采样到处理分辨率 (max 640)
    cv::Mat grayCur = rgbaToGray(rgbaData, width, height);

    // 降采样目标：长边不超过640
    const int kMaxDim = 640;
    int procW = grayCur.cols, procH = grayCur.rows;
    if (std::max(procW, procH) > kMaxDim) {
        double scale = (double)kMaxDim / std::max(procW, procH);
        procW = (int)(procW * scale);
        procH = (int)(procH * scale);
        cv::resize(grayCur, grayCur, cv::Size(procW, procH));
    }

    // 确保当前帧与参考帧尺寸一致
    if (grayCur.rows != m_refGray.rows || grayCur.cols != m_refGray.cols) {
        cv::resize(grayCur, grayCur, cv::Size(m_refGray.cols, m_refGray.rows));
    }

    // dHash 结构相似度检查 — 场景完全不同时一票否决
    {
        uint64_t curHash = computeDHash(grayCur);
        int hamming = 0;
        uint64_t diff = m_refHash ^ curHash;
        while (diff) { hamming++; diff &= diff - 1; }
        if (hamming > 30) {
            result.tracking_lost = true; // 仅标记，不阻断NGA运行
        }
    }

    // 执行 NGA 核心算法
    bool dhashLost = result.tracking_lost; // 保存 dHash 判定
    result = trySiftAlignment(m_refGray, grayCur);
    result.tracking_lost = result.tracking_lost || dhashLost; // dHash 或 SIFT 任意判丢失即丢失

    // 生成引导指令
    result.commands = generateGuidanceCommands(result.dof);

    // 判定是否对齐（跟踪丢失时强制不对齐，避免误抓）
    result.is_aligned = result.tracking_lost ? false : checkAlignment(result.dof);

    // ★ 光流兜底方案的精度不足以确认对齐（噪声中位数趋近零 → 假阳性）
    //   仅当 SIFT 匹配成功且内点数充足时才信任对齐结果
    if (result.alignment_method != "SIFT" || result.match_count < 25) {
        result.is_aligned = false;
    }

    // 对齐时给出特殊提示
    if (result.tracking_lost) {
        result.commands = {"跟踪丢失！请回到目标位置附近"};
    } else if (result.is_aligned) {
        result.commands = {"100% 构图契合，正在自动抓拍..."};
    } else if (result.commands.empty()) {
        result.commands = {"请微调手机位置，使画面与目标对齐"};
    }

    return result;
}

void NgaEngine::destroy() {
    m_sift.reset();
    m_refGray.release();
    m_refHash = 0;
    m_refDescriptors.release();
    m_refKeypoints.clear();
    m_initialized = false;
    m_hasReference = false;
    m_width = 0;
    m_height = 0;
}

void NgaEngine::setAlignThresholds(double dx, double dy, double roll, double scale) {
    m_alignThresholdDx = dx;
    m_alignThresholdDy = dy;
    m_alignThresholdRoll = roll;
    m_alignThresholdScale = scale;
}

// ============================================================================
// 梯度幅值图 — 将灰度图转换为边缘结构域
// AI 风格化图和实拍图在梯度域具有相似的结构（轮廓、边界）
// ============================================================================
cv::Mat NgaEngine::computeStructuralImage(const cv::Mat& gray) {
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 1.5);
    cv::Mat gradX, gradY, mag;
    cv::Sobel(blurred, gradX, CV_32F, 1, 0, 3);
    cv::Sobel(blurred, gradY, CV_32F, 0, 1, 3);
    cv::magnitude(gradX, gradY, mag);
    cv::Mat result;
    cv::normalize(mag, result, 0, 255, cv::NORM_MINMAX, CV_8U);
    return result;
}

// ============================================================================
// dHash 差异哈希 — 结构级场景变化检测（8×8=64位）
// ============================================================================
uint64_t NgaEngine::computeDHash(const cv::Mat& gray) {
    cv::Mat small; cv::resize(gray, small, cv::Size(9, 8));
    uint64_t hash = 0;
    for (int y = 0; y < 8; y++) {
        const uchar* row = small.ptr<uchar>(y);
        for (int x = 0; x < 8; x++) {
            if (row[x] > row[x + 1]) {
                hash |= (1ULL << (y * 8 + x));
            }
        }
    }
    return hash;
}

// ============================================================================
// 自定义 findHomography (DLT + RANSAC, 仅用 opencv_core)
// 替代 cv::findHomography，不依赖 calib3d 模块
// ============================================================================

// 归一化点集：平移至质心 + 缩放至平均距离 √2
static void normalizePoints(const std::vector<cv::Point2f>& pts,
                            std::vector<cv::Point2f>& out,
                            cv::Mat& T) {
    int n = (int)pts.size();
    cv::Point2f centroid(0, 0);
    for (const auto& p : pts) centroid += p;
    centroid *= (1.0f / n);
    double meanDist = 0;
    for (const auto& p : pts)
        meanDist += cv::norm(p - centroid);
    meanDist /= n;
    double scale = std::sqrt(2.0) / std::max(meanDist, 1e-8);
    T = (cv::Mat_<double>(3, 3) << scale, 0, -scale * centroid.x,
                                    0, scale, -scale * centroid.y,
                                    0, 0, 1);
    out.resize(n);
    for (int i = 0; i < n; i++) {
        double x = pts[i].x, y = pts[i].y;
        out[i].x = (float)(scale * x - scale * centroid.x);
        out[i].y = (float)(scale * y - scale * centroid.y);
    }
}

// DLT 法：从 4 对对应点求解 3×3 单应性矩阵
static cv::Mat dltFrom4Points(const std::vector<cv::Point2f>& src,
                               const std::vector<cv::Point2f>& dst) {
    cv::Mat A(8, 9, CV_64F, cv::Scalar(0));
    for (int i = 0; i < 4; i++) {
        double x1 = src[i].x, y1 = src[i].y;
        double x2 = dst[i].x, y2 = dst[i].y;
        double* row0 = A.ptr<double>(2 * i);
        double* row1 = A.ptr<double>(2 * i + 1);
        row0[0] = 0; row0[1] = 0; row0[2] = 0;
        row0[3] = -x1; row0[4] = -y1; row0[5] = -1;
        row0[6] = y2 * x1; row0[7] = y2 * y1; row0[8] = y2;
        row1[0] = x1; row1[1] = y1; row1[2] = 1;
        row1[3] = 0; row1[4] = 0; row1[5] = 0;
        row1[6] = -x2 * x1; row1[7] = -x2 * y1; row1[8] = -x2;
    }
    cv::Mat h;
    cv::SVD::solveZ(A, h);
    return cv::Mat(3, 3, CV_64F, h.ptr<double>()).clone();
}

// RANSAC + DLT 求解单应性矩阵（仅依赖 opencv_core）
static cv::Mat findHomographyCustom(const std::vector<cv::Point2f>& src,
                                     const std::vector<cv::Point2f>& dst,
                                     std::vector<uchar>& inlierMask,
                                     double ransacReprojThreshold = 3.0,
                                     int maxIters = 300) {
    int n = (int)src.size();
    inlierMask.assign(n, 0);
    if (n < 4) return cv::Mat();

    // 归一化
    std::vector<cv::Point2f> srcNorm, dstNorm;
    cv::Mat T1, T2;
    normalizePoints(src, srcNorm, T1);
    normalizePoints(dst, dstNorm, T2);

    int bestInliers = 0;
    cv::Mat bestH;
    double bestScore = 0;
    double thresh = ransacReprojThreshold * ransacReprojThreshold;

    cv::RNG rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());

    for (int iter = 0; iter < maxIters; iter++) {
        // 随机选 4 个不重复的索引
        int idx[4];
        for (int k = 0; k < 4; k++) {
            bool dup;
            do {
                dup = false;
                idx[k] = rng.uniform(0, n);
                for (int j = 0; j < k; j++) if (idx[k] == idx[j]) { dup = true; break; }
            } while (dup);
        }

        std::vector<cv::Point2f> s4(4), d4(4);
        for (int k = 0; k < 4; k++) { s4[k] = srcNorm[idx[k]]; d4[k] = dstNorm[idx[k]]; }

        cv::Mat Hnorm = dltFrom4Points(s4, d4);
        if (Hnorm.empty()) continue;

        // 统计内点
        std::vector<uchar> curMask(n, 0);
        int curInliers = 0;
        double curScore = 0;
        for (int i = 0; i < n; i++) {
            double sx = srcNorm[i].x, sy = srcNorm[i].y, sw = 1.0;
            double xp = Hnorm.at<double>(0,0)*sx + Hnorm.at<double>(0,1)*sy + Hnorm.at<double>(0,2);
            double yp = Hnorm.at<double>(1,0)*sx + Hnorm.at<double>(1,1)*sy + Hnorm.at<double>(1,2);
            double wp = Hnorm.at<double>(2,0)*sx + Hnorm.at<double>(2,1)*sy + Hnorm.at<double>(2,2);
            if (std::abs(wp) > 1e-8) { xp /= wp; yp /= wp; }
            double dx = xp - dstNorm[i].x, dy = yp - dstNorm[i].y;
            double err = dx * dx + dy * dy;
            if (err < thresh) {
                curMask[i] = 1; curInliers++;
                curScore += (thresh - err) / thresh;
            }
        }

        if (curInliers > bestInliers || (curInliers == bestInliers && curScore > bestScore)) {
            bestInliers = curInliers;
            bestScore = curScore;
            bestH = Hnorm.clone();
            inlierMask = curMask;
        }
    }

    if (bestInliers < 4) return cv::Mat();

    // 用所有内点重拟合
    std::vector<cv::Point2f> sIn, dIn;
    for (int i = 0; i < n; i++) {
        if (inlierMask[i]) { sIn.push_back(srcNorm[i]); dIn.push_back(dstNorm[i]); }
    }
    if (sIn.size() >= 4) {
        cv::Mat A(2 * (int)sIn.size(), 9, CV_64F, cv::Scalar(0));
        for (size_t i = 0; i < sIn.size(); i++) {
            double x1 = sIn[i].x, y1 = sIn[i].y, sw = 1.0;
            double x2 = dIn[i].x, y2 = dIn[i].y;
            double* r0 = A.ptr<double>(2 * i);
            double* r1 = A.ptr<double>(2 * i + 1);
            r0[0]=0; r0[1]=0; r0[2]=0; r0[3]=-x1; r0[4]=-y1; r0[5]=-1; r0[6]=y2*x1; r0[7]=y2*y1; r0[8]=y2;
            r1[0]=x1; r1[1]=y1; r1[2]=1; r1[3]=0; r1[4]=0; r1[5]=0; r1[6]=-x2*x1; r1[7]=-x2*y1; r1[8]=-x2;
        }
        cv::Mat hRefit;
        cv::SVD::solveZ(A, hRefit);
        cv::Mat Hrefit_norm(3, 3, CV_64F, hRefit.ptr<double>());
        // 反归一化
        cv::Mat T2inv;
        cv::invert(T2, T2inv);
        bestH = T2inv * Hrefit_norm * T1;
        bestH /= bestH.at<double>(2, 2);
    } else {
        // 反归一化最佳 H
        cv::Mat T2inv;
        cv::invert(T2, T2inv);
        bestH = T2inv * bestH * T1;
        bestH /= bestH.at<double>(2, 2);
    }

    return bestH;
}

// ============================================================================
// 尝试一：高精特征配准 (SIFT + Homography)
// 对应 nga.py 第70-115行
// ============================================================================

NgaResult NgaEngine::trySiftAlignment(const cv::Mat& grayRef, const cv::Mat& grayCur) {
    NgaResult result;

    // -- 步骤1：在梯度域检测当前帧的SIFT特征点（与参考帧一致） --
    cv::Mat structCur = computeStructuralImage(grayCur);
    std::vector<cv::KeyPoint> kpCur;
    cv::Mat desCur;
    m_sift->detectAndCompute(structCur, cv::noArray(), kpCur, desCur);

    // -- 步骤2：判断是否满足SIFT特征匹配条件 --
    bool useFallback = false;

    if (m_refDescriptors.empty() || desCur.empty() ||
        m_refDescriptors.rows < 15 || desCur.rows < 15) {
        useFallback = true;
    }

    std::vector<cv::DMatch> goodMatches;

    if (!useFallback) {
        // 转换为 CV_32F (与Python版一致)
        cv::Mat desRefFloat, desCurFloat;
        m_refDescriptors.convertTo(desRefFloat, CV_32F);
        desCur.convertTo(desCurFloat, CV_32F);

        // -- 步骤3：BF + kNN匹配 (Ratio Test) --
        // 对应Python: bf = cv2.BFMatcher(cv2.NORM_L2); matches = bf.knnMatch(des_ref, des_cur, k=2)
        cv::BFMatcher bf(cv::NORM_L2);
        std::vector<std::vector<cv::DMatch>> knnMatches;
        bf.knnMatch(desRefFloat, desCurFloat, knnMatches, 2);

        // Ratio Test: m.distance < 0.75 * n.distance
        for (const auto& matchPair : knnMatches) {
            if (matchPair.size() == 2) {
                if (matchPair[0].distance < m_ratioTestThreshold * matchPair[1].distance) {
                    goodMatches.push_back(matchPair[0]);
                }
            }
        }

        if (goodMatches.size() < static_cast<size_t>(m_minGoodMatches)) {
            useFallback = true;
        }
    }

    // 匹配质量检测：中位距离过高 = 随机假匹配
    double medDist = 0.0;
    bool lowQuality = false;
    if (!useFallback) {
        std::vector<double> distVals; distVals.reserve(goodMatches.size());
        for (const auto& m : goodMatches) { distVals.push_back(m.distance); }
        medDist = median(distVals);
        lowQuality = (medDist > 200.0); // 描述子距离>150 = 假匹配
        if (lowQuality) {
            useFallback = true; // 强制降级到光流
        }
    }

    if (!useFallback) {
        // 提取匹配点对
        std::vector<cv::Point2f> ptsRef, ptsCur;
        ptsRef.reserve(goodMatches.size());
        ptsCur.reserve(goodMatches.size());
        for (const auto& m : goodMatches) {
            ptsRef.push_back(m_refKeypoints[m.queryIdx].pt);
            ptsCur.push_back(kpCur[m.trainIdx].pt);
        }

        // 自定义 RANSAC 单应性矩阵（无 calib3d 依赖）
        std::vector<uchar> inlierMask;
        cv::Mat H = findHomographyCustom(ptsCur, ptsRef, inlierMask, m_ransacThreshold, 300);

        int inlierCount = 0;
        for (uchar v : inlierMask) if (v) inlierCount++;

        if (H.empty() || inlierCount < m_minGoodMatches) {
            useFallback = true;
        } else {
            // 从单应性矩阵提取完整 8DOF（与 Python nga.py 一致）
            double deltaX = H.at<double>(0, 2);
            double deltaY = H.at<double>(1, 2);
            double roll   = std::atan2(H.at<double>(1, 0), H.at<double>(0, 0)) * 180.0 / CV_PI;
            double scale  = std::sqrt(H.at<double>(0, 0) * H.at<double>(0, 0) +
                                     H.at<double>(1, 0) * H.at<double>(1, 0));

            result.dof.dX_px = std::round(deltaX * 1000.0) / 1000.0;
            result.dof.dY_px = std::round(deltaY * 1000.0) / 1000.0;
            result.dof.dRoll_deg = std::round(roll * 1000.0) / 1000.0;
            result.dof.dPitch_warp = std::round(H.at<double>(2, 1) * 1000.0 * 10000.0) / 10000.0;
            result.dof.dYaw_warp   = std::round(H.at<double>(2, 0) * 1000.0 * 10000.0) / 10000.0;
            result.dof.scale_factor = std::round(scale * 10000.0) / 10000.0;
            result.dof.perspective_ratio = 1.0;
            result.dof.is_pure_zoom = false;

            result.match_count = inlierCount;
            result.tracking_lost = (inlierCount < 5);
            result.alignment_method = "SIFT";
            result.success = true;
        }
    }

    // -- 步骤6：SIFT失败时降级到稠密光流兜底 --
    if (useFallback) {
        result.dof = fallbackDenseGeometry(grayRef, grayCur);
        result.tracking_lost = (std::abs(result.dof.dX_px) > 50.0 ||
                                std::abs(result.dof.dY_px) > 50.0 ||
                                std::abs(result.dof.scale_factor - 1.0) > 0.5);
        result.alignment_method = "DEPTH_FALLBACK";
        result.success = true;
    }

    return result;
}

// ============================================================================
// 尝试二：稠密几何兜底方案 (Farneback光流)
// 对应 nga.py fallback_dense_geometry_alignment()
// ============================================================================

Nga6DofResult NgaEngine::fallbackDenseGeometry(const cv::Mat& grayRef, const cv::Mat& grayCur) {
    Nga6DofResult dof;

    // -- Farneback稠密光流计算 --
    // 对应Python: cv2.calcOpticalFlowFarneback(gray_ref, gray_cur, None,
    //     pyr_scale=0.5, levels=3, winsize=15, iterations=3, poly_n=5, poly_sigma=1.2, flags=0)
    cv::Mat flow;
    cv::calcOpticalFlowFarneback(grayRef, grayCur, flow,
                                  0.5,   // pyr_scale
                                  3,     // levels
                                  15,    // winsize
                                  3,     // iterations
                                  5,     // poly_n
                                  1.2,   // poly_sigma
                                  0);    // flags

    // 提取 X 和 Y 方向分量
    std::vector<cv::Mat> flowChannels;
    cv::split(flow, flowChannels);
    cv::Mat flowX = flowChannels[0];  // X方向运动
    cv::Mat flowY = flowChannels[1];  // Y方向运动

    // 计算中位数 (滤除局部噪点和移动物体)
    // 对应Python: np.median(flow_x), np.median(flow_y)
    std::vector<double> flowXVals, flowYVals;
    flowXVals.reserve(flowX.total());
    flowYVals.reserve(flowY.total());

    // 采样策略：为提高性能，对全图进行步长采样
    int step = std::max(1, std::min(flowX.cols, flowX.rows) / 100);
    for (int y = 0; y < flowX.rows; y += step) {
        for (int x = 0; x < flowX.cols; x += step) {
            flowXVals.push_back(static_cast<double>(flowX.at<float>(y, x)));
            flowYVals.push_back(static_cast<double>(flowY.at<float>(y, x)));
        }
    }

    dof.dX_px = std::round(median(flowXVals) * 1000.0) / 1000.0;
    dof.dY_px = std::round(median(flowYVals) * 1000.0) / 1000.0;

    // -- 径向发散度计算缩放 --
    // 对应Python: 基于图像中心到边缘的向量发散度
    int h = grayRef.rows;
    int w = grayRef.cols;
    double centerX = w / 2.0;
    double centerY = h / 2.0;

    std::vector<double> radialFlows;
    radialFlows.reserve(flowXVals.size());

    for (int y = 0; y < flowX.rows; y += step) {
        for (int x = 0; x < flowX.cols; x += step) {
            double vecX = x - centerX;
            double vecY = y - centerY;
            double norm = std::sqrt(vecX * vecX + vecY * vecY) + 1e-5;

            double radialFlow = (flowX.at<float>(y, x) * vecX +
                                 flowY.at<float>(y, x) * vecY) / norm;
            radialFlows.push_back(radialFlow);
        }
    }

    double radialDivergence = median(radialFlows);
    // 每发散1像素 ≈ 0.1%焦距/深度变化
    dof.scale_factor = std::round((1.0 + radialDivergence * 0.001) * 10000.0) / 10000.0;

    // 从光流场估算 pitch / yaw（垂直/水平透视梯度）
    {
        std::vector<double> pitchVals, yawVals;
        for (int y = 0; y < flowX.rows; y += step) {
            for (int x = 0; x < flowX.cols; x += step) {
                double ddy = flowY.at<float>(y, x) - dof.dY_px;
                double ddx = flowX.at<float>(y, x) - dof.dX_px;
                double relY = (y - h * 0.5) / (h * 0.5);
                double relX = (x - w * 0.5) / (w * 0.5);
                pitchVals.push_back(ddy * relY);
                yawVals.push_back(ddx * relX);
            }
        }
        dof.dPitch_warp = std::round(median(pitchVals) * 100.0) / 100.0;
        dof.dYaw_warp = std::round(median(yawVals) * 100.0) / 100.0;
    }
    dof.dRoll_deg = 0.0;
    dof.perspective_ratio = dof.scale_factor; // 兜底方案近似等同
    dof.is_pure_zoom = false;

    return dof;
}

// ============================================================================
// 生成用户引导指令
// 对应 nga.py 第131-144行
// ============================================================================

std::vector<std::string> NgaEngine::generateGuidanceCommands(const Nga6DofResult& dof) {
    std::vector<std::string> commands;

    // 水平方向引导 (dX: 正=当前偏右，需要向左移)
    if (std::abs(dof.dX_px) > m_guidanceDxThreshold) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        if (dof.dX_px > 0) {
            oss << "← 向左微挪手机 " << std::abs(dof.dX_px) << "px";
        } else {
            oss << "→ 向右微挪手机 " << std::abs(dof.dX_px) << "px";
        }
        commands.push_back(oss.str());
    }

    // 垂直方向引导 (dY: 正=当前偏下，需要向上移)
    if (std::abs(dof.dY_px) > m_guidanceDyThreshold) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        if (dof.dY_px > 0) {
            oss << "↑ 向上微抬手机 " << std::abs(dof.dY_px) << "px";
        } else {
            oss << "↓ 向下微降手机 " << std::abs(dof.dY_px) << "px";
        }
        commands.push_back(oss.str());
    }

    // 旋转方向引导
    if (std::abs(dof.dRoll_deg) > m_guidanceRollThreshold) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        if (dof.dRoll_deg > 0) {
            oss << "↻ 向右顺时针调正 " << std::abs(dof.dRoll_deg) << "°";
        } else {
            oss << "↺ 向左逆时针调正 " << std::abs(dof.dRoll_deg) << "°";
        }
        commands.push_back(oss.str());
    }

    // 俯仰引导
    if (std::abs(dof.dPitch_warp) > 0.5) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        if (dof.dPitch_warp > 0) {
            oss << "🔽 手机前倾(俯) " << std::abs(dof.dPitch_warp);
        } else {
            oss << "🔼 手机后仰(仰) " << std::abs(dof.dPitch_warp);
        }
        commands.push_back(oss.str());
    }

    // 偏航引导
    if (std::abs(dof.dYaw_warp) > 0.5) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1);
        if (dof.dYaw_warp > 0) {
            oss << "◀ 手机左转(偏航) " << std::abs(dof.dYaw_warp);
        } else {
            oss << "▶ 手机右转(偏航) " << std::abs(dof.dYaw_warp);
        }
        commands.push_back(oss.str());
    }

    // 缩放引导
    if (std::abs(dof.scale_factor - 1.0) > m_guidanceScaleThreshold) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2);
        if (dof.scale_factor > 1.0) {
            oss << "🔍 焦距偏小：双指放大至 " << dof.scale_factor << " 倍";
        } else {
            oss << "🔎 焦距偏大：双指缩小至 " << dof.scale_factor << " 倍";
        }
        commands.push_back(oss.str());
    }

    return commands;
}

// ============================================================================
// 对齐判定
// ============================================================================

bool NgaEngine::checkAlignment(const Nga6DofResult& dof) {
    // 所有6DOF参数都在阈值内 → 判定为对齐
    bool dxOk = std::abs(dof.dX_px) < m_alignThresholdDx;
    bool dyOk = std::abs(dof.dY_px) < m_alignThresholdDy;
    bool rollOk = std::abs(dof.dRoll_deg) < m_alignThresholdRoll;
    bool scaleOk = std::abs(dof.scale_factor - 1.0) < m_alignThresholdScale;
    bool pitchOk = std::abs(dof.dPitch_warp) < 1.5;
    bool yawOk = std::abs(dof.dYaw_warp) < 1.5;

    return dxOk && dyOk && rollOk && scaleOk && pitchOk && yawOk;
}

// ============================================================================
// 工具函数
// ============================================================================

cv::Mat NgaEngine::rgbaToGray(const uint8_t* rgbaData, int width, int height) {
    // 直接从RGBA创建4通道Mat
    cv::Mat rgba(height, width, CV_8UC4, const_cast<uint8_t*>(rgbaData));
    cv::Mat gray;
    cv::cvtColor(rgba, gray, cv::COLOR_RGBA2GRAY);
    return gray.clone(); // 克隆以确保数据独立
}

double NgaEngine::median(const std::vector<double>& data) {
    if (data.empty()) {
        return 0.0;
    }
    std::vector<double> sorted = data;
    size_t n = sorted.size();
    size_t mid = n / 2;
    std::nth_element(sorted.begin(), sorted.begin() + mid, sorted.end());
    if (n % 2 == 0) {
        double a = sorted[mid];
        std::nth_element(sorted.begin(), sorted.begin() + mid - 1, sorted.end());
        double b = sorted[mid - 1];
        return (a + b) / 2.0;
    }
    return sorted[mid];
}

// ============================================================================
// 全局实例 (供 NAPI 调用)
// ============================================================================
// 使用全局指针，由NAPI层管理生命周期

namespace {
    NgaEngine* g_ngaEngine = nullptr;
}

extern "C" {

    NgaEngine* GetNgaEngine() {
        if (g_ngaEngine == nullptr) {
            g_ngaEngine = new NgaEngine();
        }
        return g_ngaEngine;
    }

    void ReleaseNgaEngine() {
        if (g_ngaEngine != nullptr) {
            g_ngaEngine->destroy();
            delete g_ngaEngine;
            g_ngaEngine = nullptr;
        }
    }

} // extern "C"
