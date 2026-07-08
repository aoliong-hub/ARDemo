import { image } from '@kit.ImageKit';
import { ScoreResult } from './Da3Service';

export interface Nga6DofResult {
  dX_px: number;
  dY_px: number;
  dRoll_deg: number;
  dPitch_warp: number;
  dYaw_warp: number;
  scale_factor: number;
  perspective_ratio: number;
  is_pure_zoom: boolean;
}

export interface NgaResult {
  success: boolean;
  alignment_method: string;
  error_reason: string;
  dof: Nga6DofResult;
  agent_commands: string[];
  is_aligned: boolean;
  tracking_lost: boolean;
  match_count: number;
}

/**
 * 当次目标会话内一张拍摄记录。
 * thumb: 用于底部抽屉展示的缩略图 PixelMap（本地生成，立即可用）。
 * score: 云端打分结果（异步到达，null 表示尚未返回）。
 * scoreLoading/scoreError: 打分 API 状态标志。
 */
export interface SessionPhoto {
  thumb: image.PixelMap;
  score: ScoreResult | null;
  scoreLoading: boolean;
  scoreError: boolean;
}
