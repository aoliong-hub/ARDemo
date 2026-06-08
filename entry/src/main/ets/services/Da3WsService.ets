/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

// v13 (e) — WebSocket 机位引导客户端。替代 HTTP getGuide 的两段式（infer → poll），
// 直连云侧 /ws/infer，利用滑动窗口多帧 batch 推理 + GlobalAligner 全局对齐。
//
// 二进制帧协议（端 → 云）：
//   [1B: type=0x01][4B: seq BE][4B: ts_ms BE][JPEG bytes...]
//
// 控制消息（端 → 云）：
//   [1B: type=0x02][4B: 0][4B: 0][1B: action=0x01=RESET]
//
// 云 → 端（文本 JSON）：
//   {"type":"status","status":"ready"}
//   {"type":"pose","frame_seq":N,...,"guide_string":{...}}
//   {"type":"error","code":"...","detail":"..."}

import webSocket from '@ohos.net.webSocket';
import { BusinessError } from '@kit.BasicServicesKit';
import { systemDateTime } from '@kit.BasicServicesKit';
import { Logger } from '../utils/Logger';
import { getCloudConfig } from '../config/CloudConfig';

const TAG = 'ARDA3-WS';

// ── 常量（协议层） ────────────────────────────────────────────
const MSG_FRAME: number = 0x01;
const MSG_CONTROL: number = 0x02;
const MSG_FRAME_WITH_POSE: number = 0x04;
const CTRL_RESET: number = 0x01;
const HEADER_SIZE: number = 9;      // 1 type + 4 seq + 4 timestamp_ms
const POSE_HEADER_SIZE: number = 37; // 9 + 7×float32 (tx,ty,tz,qx,qy,qz,qw)

// ── 接收数据类型 ──────────────────────────────────────────────

// 云侧 pose 响应（包含 guide_string）
export interface WsPoseResult {
  frame_seq: number;
  timestamp_ms: number;
  translation: number[];
  quaternion: number[];
  current_translation: number[];
  current_quaternion: number[];
  fov: number[];
  guide_string?: Record<string, ESObject>;
  guidance?: string;
  infer_count?: number;
  in_deadzone?: boolean;
}

// guide_string 的实际字段（云侧 JSON 中返回的中文方向串）
export interface WsGuideString {
  x: string;
  y: string;
  z: string;
  yaw: string;
  pitch: string;
  roll: string;
  zoom: string;
}

// 从 JSON record 中安全提取一个 string 字段
export function extractGuideField(gs: Record<string, ESObject>, key: string, defVal: string): string {
  const v: ESObject = gs[key];
  if (typeof v === 'string') {
    return v as string;
  }
  return defVal;
}

// 回调类型
export type WsStatusCallback = (status: string) => void;
export type WsPoseCallback = (pose: WsPoseResult) => void;
export type WsClosedCallback = () => void;
export type WsImageCallback = (jpegBytes: ArrayBuffer) => void;

// ── WebSocket 引导客户端 ─────────────────────────────────────

export class WsGuideClient {
  private ws: webSocket.WebSocket | null = null;
  private seq: number = 0;
  private connected: boolean = false;
  private onStatus: WsStatusCallback | null = null;
  private onPose: WsPoseCallback | null = null;
  private onClosed: WsClosedCallback | null = null;
  private onImage: WsImageCallback | null = null;

  setCallbacks(onStatus: WsStatusCallback | null, onPose: WsPoseCallback | null,
              onClosed: WsClosedCallback | null, onImage: WsImageCallback | null): void {
    this.onStatus = onStatus;
    this.onPose = onPose;
    this.onClosed = onClosed;
    this.onImage = onImage;
  }

  connect(): Promise<boolean> {
    return new Promise<boolean>((resolve: (v: boolean) => void) => {
      try {
        this.ws = webSocket.createWebSocket();
        const url: string = getCloudConfig().wsUrl;
        Logger.info(`${TAG} connecting to ${url}`);

        this.ws.connect(url, (err: BusinessError, value: boolean) => {
          if (err) {
            Logger.error(`${TAG} connect FAIL: code=${err.code} msg=${err.message}`);
            resolve(false);
            return;
          }
          Logger.info(`${TAG} connect OK ready=${value}`);

          // 注册事件
          this.ws!.on('message', (err: BusinessError, value: string | ArrayBuffer) => {
            if (err) {
              Logger.error(`${TAG} onMessage error: ${err.message}`);
              return;
            }
            this._handleMessage(value);
          });

          this.ws!.on('close', (err: BusinessError, value: webSocket.CloseResult) => {
            Logger.info(`${TAG} closed code=${value.code} reason=${value.reason}`);
            this.connected = false;
            if (this.onClosed) {
              this.onClosed();
            }
          });

          this.ws!.on('error', (err: BusinessError) => {
            Logger.error(`${TAG} error: code=${err.code} msg=${err.message}`);
          });

          this.connected = true;
          resolve(true);
        });
      } catch (e) {
        const exc: BusinessError = e as BusinessError;
        Logger.error(`${TAG} connect exception: ${exc.message}`);
        resolve(false);
      }
    });
  }

  // 发送一帧 JPEG（按协议打包二进制帧头）
  sendFrame(jpegBytes: ArrayBuffer): boolean {
    if (!this.ws || !this.connected) {
      return false;
    }

    this.seq++;
    const ts: number = Math.floor(systemDateTime.getTime(false));
    const payloadLen: number = jpegBytes.byteLength;
    const totalLen: number = HEADER_SIZE + payloadLen;

    // 打包 header + body 到一个 ArrayBuffer
    const packet: ArrayBuffer = new ArrayBuffer(totalLen);
    const view: DataView = new DataView(packet);
    view.setUint8(0, MSG_FRAME);
    view.setUint32(1, this.seq, false);   // big-endian
    view.setUint32(5, ts, false);

    // 拷贝 JPEG 字节
    const pkt8: Uint8Array = new Uint8Array(packet);
    const jpg8: Uint8Array = new Uint8Array(jpegBytes);
    pkt8.set(jpg8, HEADER_SIZE);

    this.ws.send(packet, (err: BusinessError) => {
      if (err) {
        Logger.error(`${TAG} send frame ${this.seq} FAIL: ${err.message}`);
      }
    });

    Logger.info(`${TAG} sent frame seq=${this.seq} ts=${ts} jpeg=${payloadLen}B`);
    return true;
  }

  // 发送带 ARKit pose 的帧（0x04）：附带 7×float32 world pose（tx,ty,tz,qx,qy,qz,qw）。
  // 当 AR tracking 可靠时调用；tracking 丢失时改调 sendFrame（降级为 0x01）。
  sendFrameWithPose(jpegBytes: ArrayBuffer,
                    tx: number, ty: number, tz: number,
                    qx: number, qy: number, qz: number, qw: number): boolean {
    if (!this.ws || !this.connected) {
      return false;
    }

    this.seq++;
    const ts: number = Math.floor(systemDateTime.getTime(false));
    const totalLen: number = POSE_HEADER_SIZE + jpegBytes.byteLength;

    const packet: ArrayBuffer = new ArrayBuffer(totalLen);
    const view: DataView = new DataView(packet);
    view.setUint8(0, MSG_FRAME_WITH_POSE);
    view.setUint32(1, this.seq, false);
    view.setUint32(5, ts, false);
    view.setFloat32(9,  tx, false);
    view.setFloat32(13, ty, false);
    view.setFloat32(17, tz, false);
    view.setFloat32(21, qx, false);
    view.setFloat32(25, qy, false);
    view.setFloat32(29, qz, false);
    view.setFloat32(33, qw, false);

    const pkt8: Uint8Array = new Uint8Array(packet);
    const jpg8: Uint8Array = new Uint8Array(jpegBytes);
    pkt8.set(jpg8, POSE_HEADER_SIZE);

    this.ws.send(packet, (err: BusinessError) => {
      if (err) {
        Logger.error(`${TAG} send frame+pose ${this.seq} FAIL: ${err.message}`);
      }
    });

    Logger.info(`${TAG} sent frame+pose seq=${this.seq} ts=${ts} jpeg=${jpegBytes.byteLength}B ` +
                `t=[${tx.toFixed(3)},${ty.toFixed(3)},${tz.toFixed(3)}]`);
    return true;
  }

  // 发送 RESET 控制消息
  sendReset(): void {
    if (!this.ws || !this.connected) {
      return;
    }
    const pkt: ArrayBuffer = new ArrayBuffer(HEADER_SIZE + 1);
    const view: DataView = new DataView(pkt);
    view.setUint8(0, MSG_CONTROL);
    view.setUint32(1, 0, false);
    view.setUint32(5, 0, false);
    view.setUint8(9, CTRL_RESET);

    this.ws.send(pkt, (err: BusinessError) => {
      if (err) {
        Logger.error(`${TAG} send reset FAIL: ${err.message}`);
      }
    });
    Logger.info(`${TAG} sent reset`);
  }

  close(): void {
    if (this.ws && this.connected) {
      this.connected = false;
      this.ws.close({ code: 1000, reason: 'client done' }, (err: BusinessError) => {
        if (err) {
          Logger.warn(`${TAG} close error: ${err.message}`);
        }
      });
    }
  }

  // ── 内部方法 ──────────────────────────────────────────────

  private _handleMessage(value: string | ArrayBuffer): void {
    if (typeof value === 'string') {
      this._handleText(value);
    } else {
      // 二进制消息：云侧下发的生成图（type=0x03, 9B header + JPEG payload）
      const buf: ArrayBuffer = value as ArrayBuffer;
      if (buf.byteLength > HEADER_SIZE) {
        const view: DataView = new DataView(buf);
        const msgType: number = view.getUint8(0);
        if (msgType === 0x03 && this.onImage) {
          const seq: number = view.getUint32(1, false);
          const jpegLen: number = buf.byteLength - HEADER_SIZE;
          const jpeg: ArrayBuffer = new ArrayBuffer(jpegLen);
          const dst: Uint8Array = new Uint8Array(jpeg);
          const src: Uint8Array = new Uint8Array(buf, HEADER_SIZE, jpegLen);
          dst.set(src);
          Logger.info(`${TAG} recv generated image seq=${seq} jpeg=${jpegLen}B`);
          this.onImage(jpeg);
        } else {
          Logger.info(`${TAG} recv binary ${buf.byteLength}B type=0x${msgType.toString(16)} (no handler)`);
        }
      } else {
        Logger.info(`${TAG} recv binary ${buf.byteLength}B (too short)`);
      }
    }
  }

  private _handleText(text: string): void {
    try {
      const msg: Record<string, ESObject> = JSON.parse(text) as Record<string, ESObject>;
      const msgType: string = (msg['type'] as string) || '';

      if (msgType === 'status') {
        const s: string = (msg['status'] as string) || '';
        Logger.info(`${TAG} recv status: ${s}`);
        if (this.onStatus) {
          this.onStatus(s);
        }
      } else if (msgType === 'pose') {
        const guide: Record<string, ESObject> | undefined =
          msg['guide_string'] !== undefined ? msg['guide_string'] as Record<string, ESObject> : undefined;
        if (guide !== undefined) {
          Logger.info(`${TAG} recv pose frame_seq=${msg['frame_seq']} guide=${JSON.stringify(guide)}`);
        } else {
          Logger.info(`${TAG} recv pose frame_seq=${msg['frame_seq']} (no guide_string)`);
        }
        if (this.onPose) {
          this.onPose(msg as Record<string, ESObject> as WsPoseResult);
        }
      } else if (msgType === 'vision') {
        Logger.info(`${TAG} recv vision: ${text}`);
      } else if (msgType === 'error') {
        Logger.error(`${TAG} recv error: ${text}`);
      } else {
        Logger.warn(`${TAG} recv unknown type=${msgType}: ${text}`);
      }
    } catch (e) {
      Logger.warn(`${TAG} JSON parse failed: ${(e as Error).message} raw=${text.substring(0, 200)}`);
    }
  }
}
