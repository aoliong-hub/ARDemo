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
