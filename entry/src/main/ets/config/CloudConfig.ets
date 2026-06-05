/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import dataPreferences from '@ohos.data.preferences';

// 端云接口地址配置 —— 所有后端 URL 集中在这里，修改时只改本文件即可。
// 支持 da3 / vggt-omega 两套后端，UI 上可切换。
//
// da3 后端:generate 和 guide 分别在不同 host。
//   generate → intelligence-hiaicloud.hwcloudtest.cn
//   guide    → xsjcamera.rnd.huawei.com
//
// vggt-omega 后端:统一走 175.155.64.171:61660。

export type BackendType = 'da3' | 'vggt-omega';

interface GenerateEndpoint {
  host: string;       // generate 服务的完整 host (含 scheme)
  endpoint: string;   // generate 接口路径
  taskBase: string;   // task 轮询路径前缀
}

interface GuideEndpoint {
  host: string;       // guide 服务的完整 host (含 scheme)
  inferPath: string;  // infer 接口路径
  taskPath: string;   // task 轮询路径
}

interface BackendProfile {
  name: string;             // UI 显示名
  generate: GenerateEndpoint;
  guide: GuideEndpoint;
  // WebSocket 连续引导（替代 HTTP getGuide 的两段式 infer → poll）
  wsUrl: string;
  wsGuideFrameIntervalMs: number;  // 抽帧间隔 ms
  wsGuideMaxFrames: number;        // 最大帧数（超时停止）
  pollIntervalMs: number;
  pollMaxSeconds: number;
  connectTimeoutMs: number;
  readTimeoutMs: number;
  // Debug: 跳过摄像头抽帧,直接发 rawfile 固定图到云侧
  debugUseFixedImage: boolean;
  debugFixedImageName: string;
}

// ── da3 后端 ────────────────────────────────────────────────────────
//   generate host = intelligence-hiaicloud.hwcloudtest.cn
//   guide host    = xsjcamera.rnd.huawei.com
const da3Profile: BackendProfile = {
  name: 'Da3',
  generate: {
    host: 'https://intelligence-hiaicloud.hwcloudtest.cn',
    endpoint: '/camera-agent-api/api/generate',
    taskBase: '/camera-agent-api/api/task/',
  },
  guide: {
    host: 'https://xsjcamera.rnd.huawei.com',
    inferPath: '/mrightserver/uat/camera_agent_guide/infer',
    taskPath: '/mrightserver/uat/camera_agent_guide/task',
  },
  wsUrl: 'wss://xsjcamera.rnd.huawei.com/ws/infer',
  wsGuideFrameIntervalMs: 500,
  wsGuideMaxFrames: 240,
  pollIntervalMs: 1000,
  pollMaxSeconds: 120,
  connectTimeoutMs: 30000,
  readTimeoutMs: 60000,
  debugUseFixedImage: true,
  debugFixedImageName: '20260605_161451_847d08ca_1_a1_prompt_input.jpeg',
};

// ── vggt-omega 后端 ─────────────────────────────────────────────────
//   统一 host = http://175.155.64.171:61660
//   API 路径待确认,目前用 /vggt_ 前缀占位。
const vggtOmegaProfile: BackendProfile = {
  name: 'VGGT-Ω',
  generate: {
    host: 'http://175.155.64.171:61660',
    endpoint: '/camera-agent-api/api/generate',
    taskBase: '/camera-agent-api/api/task/',
  },
  guide: {
    host: 'http://175.155.64.171:61660',
    inferPath: '/mrightserver/uat/camera_agent_guide/infer',
    taskPath: '/mrightserver/uat/camera_agent_guide/task',
  },
  wsUrl: 'ws://175.155.64.171:61660/ws/infer',
  wsGuideFrameIntervalMs: 500,
  wsGuideMaxFrames: 240,
  pollIntervalMs: 1000,
  pollMaxSeconds: 120,
  connectTimeoutMs: 30000,
  readTimeoutMs: 60000,
  debugUseFixedImage: true,
  debugFixedImageName: '20260605_161451_847d08ca_1_a1_prompt_input.jpeg',
};

// ── 注册表 ──────────────────────────────────────────────────────────
const PROFILES: Record<BackendType, BackendProfile> = {
  'da3': da3Profile,
  'vggt-omega': vggtOmegaProfile,
};

// ── 全局当前后端(默认 da3，UI 可切换，持久化到 preferences) ─────────
const PREFS_NAME = 'cloud_config';
const PREF_KEY_BACKEND = 'backend_type';
const VALID_BACKENDS: BackendType[] = ['da3', 'vggt-omega'];

let _currentBackend: BackendType = 'da3';
let _prefsReady: boolean = false;

// 应用启动时调一次，从磁盘恢复上次选择。如果从未存过则保持默认 'da3'。
export async function initBackendType(context: Context): Promise<void> {
  try {
    const prefs = await dataPreferences.getPreferences(context, PREFS_NAME);
    const saved = await prefs.get(PREF_KEY_BACKEND, 'da3');
    if (typeof saved === 'string' && VALID_BACKENDS.includes(saved as BackendType)) {
      _currentBackend = saved as BackendType;
    }
    _prefsReady = true;
  } catch (e) {
    // 馯次安装 / preferences 损坏 → 保持默认
    _prefsReady = true;
  }
}

export function getBackendType(): BackendType {
  return _currentBackend;
}

export function setBackendType(t: BackendType, context?: Context): void {
  _currentBackend = t;
  // 异步写盘，不阻塞 UI
  if (context !== undefined && _prefsReady) {
    dataPreferences.getPreferences(context, PREFS_NAME).then((prefs) => {
      prefs.put(PREF_KEY_BACKEND, t);
      prefs.flush();
    }).catch(() => {});
  }
}

export function getCloudConfig(): BackendProfile {
  return PROFILES[_currentBackend];
}

export function getBackendLabel(t: BackendType): string {
  return PROFILES[t].name;
}

export function allBackendTypes(): BackendType[] {
  return ['da3', 'vggt-omega'];
}
