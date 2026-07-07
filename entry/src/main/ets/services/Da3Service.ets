/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

// v13 (d) — Da3 机位服务的两个裸 HTTP 客户端 (no cookie). 全程 ARDA3-HTTP 日志,失败 resolve(null)
// 不抛,保证上层 UI 不会因为网络异常而崩。
//
// generate(jpeg): 上传当前帧 + prompt → 拿 taskId → 轮询 → 拿到目标图 (base64 png) → 解 PixelMap。
// getGuide(target, current): 上传两张图 → 拿 taskid → 轮询 → 取 guide_string {x,y,z,pitch,yaw,roll,zoom}。
// scoreImage(jpeg): 上传拍摄结果图 → 同步返回美学打分 ScoreResult。
//
// 公共工具 postMultipart / get 统一打印 (URL, code, cost, bodyHead, err)。

import http from '@ohos.net.http';
import { image } from '@kit.ImageKit';
import { util } from '@kit.ArkTS';
import { fileIo } from '@kit.CoreFileKit';
import { systemDateTime } from '@kit.BasicServicesKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { Logger } from '../utils/Logger';
import { getCloudConfig, getGenerateUrl, getGenTaskBaseUrl, getScoreUrl, getGuideInferUrl, getGuideTaskUrl } from '../config/CloudConfig';

// Same shape as ARRingHuntCam's GuideData — all fields are strings (the host's applyGuide
// parses & validates them).
export interface GuideData {
  x: string;
  y: string;
  z: string;
  pitch: string;
  yaw: string;
  roll: string;
  zoom: string;
}

// One multipart part. Either text!=undefined (UTF-8 field) or bytes!=undefined (binary file).
export interface MultipartPart {
  name: string;
  text?: string;
  bytes?: ArrayBuffer;
  filename?: string;
  contentType?: string;
}

// Tag for grep: ARDA3-HTTP.
const TAG = 'ARDA3-HTTP';

// URL helpers — 全部由 CloudConfig 统一拼接后导出，此处直接代理，不做二次拼接。
function cfgGenUrl(): string { return getGenerateUrl(); }
function cfgGenTaskUrl(): string { return getGenTaskBaseUrl(); }
function cfgGuideInferUrl(): string { return getGuideInferUrl(); }
function cfgGuideTaskUrl(): string { return getGuideTaskUrl(); }
function cfgPollMs(): number { return getCloudConfig().pollIntervalMs; }
function cfgPollMaxS(): number { return getCloudConfig().pollMaxSeconds; }
function cfgConnMs(): number { return getCloudConfig().connectTimeoutMs; }
function cfgReadMs(): number { return getCloudConfig().readTimeoutMs; }

function nowMs(): number {
  return systemDateTime.getTime(false);
}

function sleepMs(ms: number): Promise<void> {
  return new Promise<void>((resolve: () => void) => {
    setTimeout(() => { resolve(); }, ms);
  });
}

function strBytes(s: string): Uint8Array {
  const enc: util.TextEncoder = new util.TextEncoder('utf-8');
  return enc.encodeInto(s);
}

function bytesToArrayBuffer(arr: Uint8Array): ArrayBuffer {
  return arr.buffer.slice(arr.byteOffset, arr.byteOffset + arr.byteLength);
}

// ----- multipart build -----
let __mpCounter: number = 0;

interface MultipartResult {
  body: ArrayBuffer;
  boundary: string;
}

function buildMultipart(parts: MultipartPart[]): MultipartResult {
  __mpCounter = __mpCounter + 1;
  const boundary: string = '----ARDA3Bnd' + nowMs().toString() + '_' + __mpCounter.toString();
  const segs: Uint8Array[] = [];
  for (const p of parts) {
    let head: string = '--' + boundary + '\r\n';
    head = head + 'Content-Disposition: form-data; name="' + p.name + '"';
    if (p.filename !== undefined) {
      head = head + '; filename="' + p.filename + '"';
    }
    head = head + '\r\n';
    if (p.contentType !== undefined) {
      head = head + 'Content-Type: ' + p.contentType + '\r\n';
    }
    head = head + '\r\n';
    segs.push(strBytes(head));
    if (p.bytes !== undefined) {
      segs.push(new Uint8Array(p.bytes));
    } else if (p.text !== undefined) {
      segs.push(strBytes(p.text));
    }
    segs.push(strBytes('\r\n'));
  }
  segs.push(strBytes('--' + boundary + '--\r\n'));
  let total: number = 0;
  for (const s of segs) {
    total = total + s.byteLength;
  }
  const out: Uint8Array = new Uint8Array(total);
  let off: number = 0;
  for (const s of segs) {
    out.set(s, off);
    off = off + s.byteLength;
  }
  const result: MultipartResult = { body: bytesToArrayBuffer(out), boundary: boundary };
  return result;
}

function truncate(s: string, n: number): string {
  if (s.length <= n) {
    return s;
  }
  return s.substring(0, n);
}

// HiLog 遇 \n 会把当条日志在换行处截断,所以打 body 前必须把所有空白折成单行,
// 否则只能看到第一行 "{"。把 \r\n / \n 折成字面 "\\n",其他连续空白折成单空格。
function safeForLog(s: string): string {
  return String(s).replace(/\r?\n/g, '\\n').replace(/\s+/g, ' ');
}

function resultToString(r: string | Object | ArrayBuffer): string {
  if (typeof r === 'string') {
    return r as string;
  }
  if (r instanceof ArrayBuffer) {
    const dec: util.TextDecoder = util.TextDecoder.create('utf-8');
    return dec.decodeToString(new Uint8Array(r));
  }
  try {
    return JSON.stringify(r);
  } catch (e) {
    return '[unserializable]';
  }
}

// ----- low-level helpers -----

interface HttpResult {
  code: number;
  body: string;
  ms: number;
}

function postMultipart(url: string, parts: MultipartPart[], extraHeaders?: Record<string, string>): Promise<HttpResult | null> {
  const mp: MultipartResult = buildMultipart(parts);
  const headers: Record<string, string> = {
    'Content-Type': 'multipart/form-data; boundary=' + mp.boundary,
    'Accept': 'application/json'
  };
  if (extraHeaders !== undefined) {
    const keys: string[] = Object.keys(extraHeaders);
    for (const k of keys) {
      headers[k] = extraHeaders[k];
    }
  }
  Logger.info(`${TAG} POST url=${url} bodyBytes=${mp.body.byteLength} boundary=${mp.boundary}`);
  return new Promise<HttpResult | null>((resolve: (v: HttpResult | null) => void) => {
    const start: number = nowMs();
    const req: http.HttpRequest = http.createHttp();
    req.request(url, {
      method: http.RequestMethod.POST,
      header: headers,
      extraData: mp.body,
      connectTimeout: cfgConnMs(),
      readTimeout: cfgReadMs()
    }, (err: BusinessError, data: http.HttpResponse) => {
      const ms: number = nowMs() - start;
      try {
        if (err) {
          Logger.error(`${TAG} POST FAIL url=${url} cost=${ms}ms err.code=${err.code} err.msg=${err.message}`);
          resolve(null);
          return;
        }
        const code: number = data.responseCode;
        const bodyStr: string = resultToString(data.result);
        Logger.info(`${TAG} POST resp url=${url} code=${code} cost=${ms}ms body=${truncate(safeForLog(bodyStr), 1200)}`);
        resolve({ code: code, body: bodyStr, ms: ms });
      } finally {
        req.destroy();
      }
    });
  });
}

function get(url: string, extraHeaders?: Record<string, string>): Promise<HttpResult | null> {
  const headers: Record<string, string> = { 'Accept': 'application/json' };
  if (extraHeaders !== undefined) {
    const keys: string[] = Object.keys(extraHeaders);
    for (const k of keys) {
      headers[k] = extraHeaders[k];
    }
  }
  Logger.info(`${TAG} GET url=${url}`);
  return new Promise<HttpResult | null>((resolve: (v: HttpResult | null) => void) => {
    const start: number = nowMs();
    const req: http.HttpRequest = http.createHttp();
    req.request(url, {
      method: http.RequestMethod.GET,
      header: headers,
      connectTimeout: cfgConnMs(),
      readTimeout: cfgReadMs()
    }, (err: BusinessError, data: http.HttpResponse) => {
      const ms: number = nowMs() - start;
      try {
        if (err) {
          Logger.error(`${TAG} GET FAIL url=${url} cost=${ms}ms err.code=${err.code} err.msg=${err.message}`);
          resolve(null);
          return;
        }
        const code: number = data.responseCode;
        const bodyStr: string = resultToString(data.result);
        Logger.info(`${TAG} GET resp url=${url} code=${code} cost=${ms}ms body=${truncate(safeForLog(bodyStr), 1200)}`);
        resolve({ code: code, body: bodyStr, ms: ms });
      } finally {
        req.destroy();
      }
    });
  });
}

// ----- JSON helpers (permissive: server schema is not fully pinned yet) -----

function safeParse(s: string): Record<string, ESObject> | null {
  try {
    const obj: ESObject = JSON.parse(s);
    return obj as Record<string, ESObject>;
  } catch (e) {
    Logger.warn(`${TAG} JSON.parse failed: ${(e as Error).message}`);
    return null;
  }
}

function pickString(obj: Record<string, ESObject> | null, keys: string[]): string | null {
  if (obj === null) {
    return null;
  }
  for (const k of keys) {
    const v: ESObject = obj[k];
    if (typeof v === 'string' && (v as string).length > 0) {
      return v as string;
    }
  }
  return null;
}

function pickNumber(obj: Record<string, ESObject> | null, keys: string[]): number | null {
  if (obj === null) {
    return null;
  }
  for (const k of keys) {
    const v: ESObject = obj[k];
    if (typeof v === 'number') {
      return v as number;
    }
  }
  return null;
}

function pickObject(obj: Record<string, ESObject> | null, key: string): Record<string, ESObject> | null {
  if (obj === null) {
    return null;
  }
  const v: ESObject = obj[key];
  if (v !== null && v !== undefined && typeof v === 'object') {
    return v as Record<string, ESObject>;
  }
  return null;
}

// Decode base64 png string -> PixelMap. Strips an optional "data:image/png;base64," prefix.
async function pixelMapFromBase64(b64: string): Promise<image.PixelMap | null> {
  let raw: string = b64;
  const commaIdx: number = raw.indexOf(',');
  if (raw.startsWith('data:') && commaIdx > 0) {
    raw = raw.substring(commaIdx + 1);
  }
  try {
    const helper: util.Base64Helper = new util.Base64Helper();
    const bytes: Uint8Array = await helper.decode(raw);
    const ab: ArrayBuffer = bytesToArrayBuffer(bytes);
    const src: image.ImageSource = image.createImageSource(ab);
    const opts: image.DecodingOptions = {
      editable: true,
      desiredPixelFormat: image.PixelMapFormat.RGBA_8888
    };
    const pm: image.PixelMap = await src.createPixelMap(opts);
    src.release();
    return pm;
  } catch (e) {
    const err: BusinessError = e as BusinessError;
    Logger.error(`${TAG} base64 decode/PixelMap failed: code=${err.code} msg=${err.message}`);
    return null;
  }
}

// Pack a PixelMap -> JPEG ArrayBuffer (quality 92). Exported so the host page can re-pack the
// generated target image into JPEG bytes for the second-stage Da3 call.
export async function packPixelMapToJpeg(pm: image.PixelMap, quality: number = 92): Promise<ArrayBuffer | null> {
  let packer: image.ImagePacker | null = null;
  try {
    packer = image.createImagePacker();
    const opts: image.PackingOption = { format: 'image/jpeg', quality: quality };
    const jpeg: ArrayBuffer = await packer.packing(pm, opts);
    return jpeg;
  } catch (e) {
    const err: BusinessError = e as BusinessError;
    Logger.error(`${TAG} packPixelMapToJpeg failed: code=${err.code} msg=${err.message}`);
    return null;
  } finally {
    if (packer !== null) {
      packer.release().catch((e: BusinessError) => { Logger.warn(`${TAG} packer.release: ${e.message}`); });
    }
  }
}

function saveBytes(filesDir: string | undefined, name: string, bytes: ArrayBuffer): void {
  if (filesDir === undefined || filesDir.length === 0) {
    return;
  }
  try {
    const path: string = filesDir + '/' + name;
    const f: fileIo.File = fileIo.openSync(path, fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
    fileIo.writeSync(f.fd, bytes);
    fileIo.closeSync(f.fd);
    Logger.info(`${TAG} saved path=${path} bytes=${bytes.byteLength}`);
  } catch (e) {
    const err: BusinessError = e as BusinessError;
    Logger.warn(`${TAG} save FAIL name=${name}: code=${err.code} msg=${err.message}`);
  }
}

// === da3 调试存档:在 filesDir/da3_debug/ 下按时间戳留痕 ===
// 用法:dbgEnsure(filesDir) 取得子目录(如不存在则建),失败返回 undefined。
function dbgEnsure(filesDir: string | undefined): string | undefined {
  if (filesDir === undefined || filesDir.length === 0) {
    return undefined;
  }
  const dir: string = filesDir + '/da3_debug';
  try {
    if (!fileIo.accessSync(dir)) {
      fileIo.mkdirSync(dir);
    }
    return dir;
  } catch (e) {
    const err: BusinessError = e as BusinessError;
    Logger.warn(`ARDA3-DBG mkdir FAIL ${dir}: code=${err.code} msg=${err.message}`);
    return undefined;
  }
}

function dbgSaveBytes(dbgDir: string | undefined, name: string, bytes: ArrayBuffer): void {
  if (dbgDir === undefined) {
    return;
  }
  try {
    const path: string = dbgDir + '/' + name;
    const f: fileIo.File = fileIo.openSync(path, fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
    fileIo.writeSync(f.fd, bytes);
    fileIo.closeSync(f.fd);
    Logger.info(`ARDA3-DBG saved ${path} bytes=${bytes.byteLength}`);
  } catch (e) {
    const err: BusinessError = e as BusinessError;
    Logger.warn(`ARDA3-DBG save FAIL ${name}: code=${err.code} msg=${err.message}`);
  }
}

function dbgSaveText(dbgDir: string | undefined, name: string, text: string): void {
  if (dbgDir === undefined) {
    return;
  }
  try {
    const path: string = dbgDir + '/' + name;
    const f: fileIo.File = fileIo.openSync(path, fileIo.OpenMode.CREATE | fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.TRUNC);
    const enc: util.TextEncoder = new util.TextEncoder('utf-8');
    const bytes: Uint8Array = enc.encodeInto(text);
    fileIo.writeSync(f.fd, bytesToArrayBuffer(bytes));
    fileIo.closeSync(f.fd);
    Logger.info(`ARDA3-DBG saved ${path} text len=${text.length}`);
  } catch (e) {
    const err: BusinessError = e as BusinessError;
    Logger.warn(`ARDA3-DBG save FAIL ${name}: code=${err.code} msg=${err.message}`);
  }
}

// 可读时间戳前缀:YYYYMMDD_HHMMSS_mmm
function dbgTs(): string {
  const ms: number = nowMs();
  // ArkTS 禁 Date.now(),但 nowMs() 给毫秒。这里用毫秒后 9 位做唯一前缀(够区分)。
  return ms.toString();
}

// ───────────────────────────────────────────────
//  Stage 1: generate(jpeg) → PixelMap | null
// ───────────────────────────────────────────────

// 宽松地把潜在 status 字段(顶层 + data.*)拉出来折成小写串数组,统一比对枚举。
function collectStatusStrings(obj: Record<string, ESObject> | null): string[] {
  const out: string[] = [];
  if (obj === null) {
    return out;
  }
  const keys: string[] = ['status', 'task_status', 'state'];
  for (const k of keys) {
    const v: ESObject = obj[k];
    if (typeof v === 'string') {
      out.push((v as string).toLowerCase());
    } else if (typeof v === 'number') {
      out.push(String(v as number));
    }
  }
  const data: Record<string, ESObject> | null = pickObject(obj, 'data');
  if (data !== null) {
    for (const k of keys) {
      const v: ESObject = data[k];
      if (typeof v === 'string') {
        out.push((v as string).toLowerCase());
      } else if (typeof v === 'number') {
        out.push(String(v as number));
      }
    }
  }
  return out;
}

// 取图字段。先看显式 key,再走"很长字符串字段"启发式(base64 png 通常 >>1000 字符)。
function findImageField(obj: Record<string, ESObject> | null): string | null {
  if (obj === null) {
    return null;
  }
  const KEYS: string[] = ['imageData', 'image_data', 'image', 'result_image', 'result', 'output'];
  for (const k of KEYS) {
    const v: ESObject = obj[k];
    if (typeof v === 'string' && (v as string).length > 100) {
      return v as string;
    }
  }
  const data: Record<string, ESObject> | null = pickObject(obj, 'data');
  if (data !== null) {
    for (const k of KEYS) {
      const v: ESObject = data[k];
      if (typeof v === 'string' && (v as string).length > 100) {
        return v as string;
      }
    }
  }
  // 启发式:任何 >1000 字符的字符串字段都当作疑似 base64 图。
  const topKeys: string[] = Object.keys(obj);
  for (const k of topKeys) {
    const v: ESObject = obj[k];
    if (typeof v === 'string' && (v as string).length > 1000) {
      Logger.info(`${TAG} image-field heuristic: top.${k} len=${(v as string).length}`);
      return v as string;
    }
  }
  if (data !== null) {
    const dKeys: string[] = Object.keys(data);
    for (const k of dKeys) {
      const v: ESObject = data[k];
      if (typeof v === 'string' && (v as string).length > 1000) {
        Logger.info(`${TAG} image-field heuristic: data.${k} len=${(v as string).length}`);
        return v as string;
      }
    }
  }
  return null;
}

async function pollGenerateTask(taskId: string): Promise<string | null> {
  const start: number = nowMs();
  const maxMs: number = cfgPollMaxS() * 1000;
  const DONE: string[] = ['success', 'succeeded', 'finished', 'finish', 'done', 'complete', 'completed', '0'];
  const FAIL: string[] = ['failed', 'fail', 'error', '1'];
  while (nowMs() - start < maxMs) {
    const resp: HttpResult | null = await get(cfgGenTaskUrl() + taskId);
    if (resp === null) {
      await sleepMs(cfgPollMs());
      continue;
    }
    const json: Record<string, ESObject> | null = safeParse(resp.body);
    Logger.info(`${TAG} generate poll code=${resp.code} body=${truncate(safeForLog(resp.body), 1200)}`);

    // success: false 明确表示服务端错误（如 "list index out of range"），立即终止轮询。
    if (json !== null && json['success'] === false) {
      Logger.error(`${TAG} generate poll server error: msg=${pickString(json, ['msg', 'message']) ?? '?'}`);
      return null;
    }

    // 任何疑似图字段非空 → 直接当完成。
    const img: string | null = findImageField(json);
    if (img !== null) {
      Logger.info(`${TAG} generate SUCCESS imageBytes=${img.length}(b64)`);
      return img;
    }

    // 状态枚举(顶层 + data.*,小写)。
    const stats: string[] = collectStatusStrings(json);
    let done: boolean = false;
    let fail: boolean = false;
    for (const s of stats) {
      if (DONE.indexOf(s) >= 0) {
        done = true;
      }
      if (FAIL.indexOf(s) >= 0) {
        fail = true;
      }
    }
    if (fail) {
      Logger.error(`${TAG} generate task FAILED stats=[${stats.join(',')}] body=${truncate(safeForLog(resp.body), 1200)}`);
      return null;
    }
    if (done) {
      Logger.error(`${TAG} generate marked DONE but no image field found body=${truncate(safeForLog(resp.body), 1200)}`);
      return null;
    }
    await sleepMs(cfgPollMs());
  }
  Logger.error(`${TAG} generate poll TIMEOUT after ${cfgPollMaxS()}s taskId=${taskId}`);
  return null;
}

export async function generate(jpeg: ArrayBuffer, taskId: string, filesDir?: string): Promise<image.PixelMap | null> {
  Logger.info(`ARDA3-FLOW Da3.generate start bytes=${jpeg.byteLength} task_id=${taskId}`);
  // generate 接口本身可能并不强需 task_id,但带上无害;getGuide 阶段服务端靠它关联此次目标图。
  const PROMPT: string = 'keep unchanged';
  const parts: MultipartPart[] = [
    { name: 'image', filename: 'image.jpg', contentType: 'image/jpeg', bytes: jpeg },
    { name: 'prompt', text: PROMPT },
    { name: 'task_id', text: taskId }
  ];
  // === 旁路存档:发出去的图 + 请求参数 ===
  const dbgDir: string | undefined = dbgEnsure(filesDir);
  const ts: string = dbgTs();
  dbgSaveBytes(dbgDir, `${ts}_gen_input.jpg`, jpeg);
  dbgSaveText(dbgDir, `${ts}_gen_req.txt`,
    `url=${cfgGenUrl()}\ntask_id=${taskId}\nprompt=${PROMPT}\ninput_image_bytes=${jpeg.byteLength}\n`);
  const resp: HttpResult | null = await postMultipart(cfgGenUrl(), parts);
  if (resp === null) {
    Logger.error(`${TAG} generate POST returned null`);
    return null;
  }
  if (resp.code < 200 || resp.code >= 300) {
    Logger.error(`${TAG} generate POST bad code=${resp.code} body=${truncate(safeForLog(resp.body), 1200)}`);
    return null;
  }
  const json: Record<string, ESObject> | null = safeParse(resp.body);
  const respTaskId: string | null = pickString(json, ['taskId', 'task_id', 'id']);
  if (respTaskId === null) {
    Logger.error(`${TAG} generate no taskId in body=${truncate(safeForLog(resp.body), 1200)}`);
    return null;
  }
  Logger.info(`${TAG} generate respTaskId=${respTaskId} (sessionTaskId=${taskId})`);
  const b64: string | null = await pollGenerateTask(respTaskId);
  if (b64 === null) {
    return null;
  }
  const pm: image.PixelMap | null = await pixelMapFromBase64(b64);
  if (pm === null) {
    return null;
  }
  // Persist the target image as PNG bytes (just dump the raw base64-decoded payload).
  try {
    const helper: util.Base64Helper = new util.Base64Helper();
    let raw: string = b64;
    const commaIdx: number = raw.indexOf(',');
    if (raw.startsWith('data:') && commaIdx > 0) {
      raw = raw.substring(commaIdx + 1);
    }
    const bytes: Uint8Array = await helper.decode(raw);
    const pngBuf: ArrayBuffer = bytesToArrayBuffer(bytes);
    saveBytes(filesDir, `target_${nowMs()}.png`, pngBuf);
    // 旁路存档:同一份 png 也存到 da3_debug,和 input.jpg 同时间戳前缀关联。
    dbgSaveBytes(dbgDir, `${ts}_gen_output.png`, pngBuf);
  } catch (e) {
    Logger.warn(`${TAG} target save skipped: ${(e as Error).message}`);
  }
  Logger.info(`ARDA3-FLOW Da3.generate SUCCESS`);
  return pm;
}

// ───────────────────────────────────────────────
//  Stage 2: getGuide(target, current) → GuideData | null
// ───────────────────────────────────────────────

// Server 返回的 guide_string 字段是带方向词 + 单位的中文友好串,例如:
//   x="右 0.15 米" / "左 0.15 米"      y="上 / 下 N 米"      z="前 / 后 N 米"
//   yaw="右转 / 左转 N°"   pitch="仰拍 / 俯拍 N°"   roll="右倾 / 左倾 N°"
//   zoom="1.03x"
// applyGuide 期望裸数字 string,所以在 getGuide 返回前先翻译一次。
// 关键约定:pitch 在这里保留 da3 原始符号"+=仰"(仰拍→正,俯拍→负),由 applyGuide 内的
// `-Number(g.pitch)` 翻成 placeRingAt 的"+=俯"。这层不要替 applyGuide 翻,会重复抵消。
function parseSignedField(raw: string, negWords: string[], posWords: string[]): string {
  const trimmed: string = String(raw).trim();
  // 已经是纯数字串:forward-compat(将来 server 改回裸数字也兼容)。
  if (/^-?\d+(\.\d+)?$/.test(trimmed)) {
    return trimmed;
  }
  const m: RegExpMatchArray | null = trimmed.match(/-?\d+(\.\d+)?/);
  if (m === null) {
    return '0';
  }
  const num: number = Number(m[0]);
  if (isNaN(num)) {
    return '0';
  }
  const abs: number = Math.abs(num);
  for (const w of negWords) {
    if (trimmed.indexOf(w) >= 0) {
      return (-abs).toString();
    }
  }
  for (const w of posWords) {
    if (trimmed.indexOf(w) >= 0) {
      return abs.toString();
    }
  }
  // 有数字但没有方向词:照原值返回。
  return num.toString();
}

function parseZoom(raw: string): string {
  const trimmed: string = String(raw).trim();
  if (/^-?\d+(\.\d+)?$/.test(trimmed)) {
    return trimmed;
  }
  const m: RegExpMatchArray | null = trimmed.match(/-?\d+(\.\d+)?/);
  if (m === null) {
    return '1';
  }
  return m[0];
}

export function parseGuideString(raw: GuideData): GuideData {
  const out: GuideData = {
    x:     parseSignedField(raw.x,     ['左'],     ['右']),
    y:     parseSignedField(raw.y,     ['下'],     ['上']),
    z:     parseSignedField(raw.z,     ['后'],     ['前']),
    yaw:   parseSignedField(raw.yaw,   ['左转'],   ['右转']),
    // 实测翻转:server "俯拍 N°" → +N,"仰拍 N°" → -N。applyGuide 的 `-Number(g.pitch)` 把它
    // 翻成 placeRingAt 期望的最终符号 → mTargetPitch 视觉/对齐方向正确。
    pitch: parseSignedField(raw.pitch, ['仰拍'],   ['俯拍']),
    roll:  parseSignedField(raw.roll,  ['左倾'],   ['右倾']),
    zoom:  parseZoom(raw.zoom)
  };
  return out;
}

function pickGuideString(out: Record<string, ESObject> | null): GuideData | null {
  if (out === null) {
    return null;
  }
  // Server may put guide_string directly, or wrap in {output:{guide_string:{...}}}, or output[0].
  let gs: ESObject = out['guide_string'];
  if (gs === undefined) {
    const inner: Record<string, ESObject> | null = pickObject(out, 'data');
    if (inner !== null) {
      gs = inner['guide_string'];
    }
  }
  if (gs === undefined || gs === null || typeof gs !== 'object') {
    return null;
  }
  const g: Record<string, ESObject> = gs as Record<string, ESObject>;
  const fields: string[] = ['x', 'y', 'z', 'pitch', 'yaw', 'roll', 'zoom'];
  const vals: Record<string, string> = {};
  for (const k of fields) {
    const v: ESObject = g[k];
    if (v === undefined || v === null) {
      Logger.error(`${TAG} guide_string missing field=${k}`);
      return null;
    }
    vals[k] = String(v);
  }
  const result: GuideData = {
    x: vals['x'], y: vals['y'], z: vals['z'],
    pitch: vals['pitch'], yaw: vals['yaw'], roll: vals['roll'],
    zoom: vals['zoom']
  };
  return result;
}

async function pollGuideTask(taskid: string): Promise<GuideData | null> {
  const start: number = nowMs();
  const maxMs: number = cfgPollMaxS() * 1000;
  const url: string = cfgGuideTaskUrl();
  while (nowMs() - start < maxMs) {
    const parts: MultipartPart[] = [{ name: 'taskid', text: taskid }];
    const resp: HttpResult | null = await postMultipart(url, parts);
    if (resp === null) {
      await sleepMs(cfgPollMs());
      continue;
    }
    const json: Record<string, ESObject> | null = safeParse(resp.body);
    const data: Record<string, ESObject> | null = pickObject(json, 'data');

    // 真实 schema: task_status 在 data.task_status,顶层做兜底。
    let st: number | null = null;
    if (data !== null) {
      st = pickNumber(data, ['task_status']);
    }
    if (st === null) {
      st = pickNumber(json, ['task_status']);
    }

    // progress: data.result.progress > data.progress > 顶层 progress。
    let progress: number | null = null;
    if (data !== null) {
      const result: Record<string, ESObject> | null = pickObject(data, 'result');
      if (result !== null) {
        progress = pickNumber(result, ['progress']);
      }
      if (progress === null) {
        progress = pickNumber(data, ['progress']);
      }
    }
    if (progress === null) {
      progress = pickNumber(json, ['progress']);
    }

    // serverError: data.error 优先,顶层 error 兜底。
    let serverError: string | null = null;
    if (data !== null) {
      serverError = pickString(data, ['error']);
    }
    if (serverError === null) {
      serverError = pickString(json, ['error']);
    }

    // is_finished 也作为结束信号(成功/失败靠 st 区分)。
    let isFinished: boolean = false;
    if (data !== null) {
      const v: ESObject = data['is_finished'];
      if (typeof v === 'boolean') {
        isFinished = v as boolean;
      }
    }

    Logger.info(`${TAG} guide poll code=${resp.code} task_status=${st ?? '?'} progress=${progress ?? '?'} is_finished=${isFinished} serverError=${serverError ?? '?'}`);

    if (st === 0) {
      // 成功:依次尝试 data.output → 顶层 output → data → 顶层 拿 guide_string。
      let out: Record<string, ESObject> | null = null;
      if (data !== null) {
        out = pickObject(data, 'output');
      }
      if (out === null) {
        out = pickObject(json, 'output');
      }
      let guide: GuideData | null = pickGuideString(out);
      if (guide === null && data !== null) {
        guide = pickGuideString(data);
      }
      if (guide === null) {
        guide = pickGuideString(json);
      }
      if (guide === null) {
        Logger.error(`${TAG} guide done but no guide_string body=${truncate(safeForLog(resp.body), 1200)}`);
        return null;
      }
      Logger.info(`${TAG} guide SUCCESS raw guide_string=${JSON.stringify(guide)}`);
      const parsed: GuideData = parseGuideString(guide);
      Logger.info(`${TAG} guide parsed x=${parsed.x} y=${parsed.y} z=${parsed.z} yaw=${parsed.yaw} pitch=${parsed.pitch} roll=${parsed.roll} zoom=${parsed.zoom}`);
      return parsed;
    }

    // 失败:task_status=1 或 is_finished=true 但不是 0,立即退出 + 把 server error 打出来。
    if (st === 1 || (isFinished && st !== 0)) {
      Logger.error(`${TAG} getGuide SERVER FAIL task_status=${st ?? '?'} error=${serverError ?? '?'} body=${truncate(safeForLog(resp.body), 1200)}`);
      return null;
    }

    // 2 = running, 3 = waiting, null = 未到 → 继续轮询。
    if (st === null) {
      Logger.warn(`${TAG} guide poll missing task_status body=${truncate(safeForLog(resp.body), 1200)}`);
    }
    await sleepMs(cfgPollMs());
  }
  Logger.error(`${TAG} guide poll TIMEOUT after ${cfgPollMaxS()}s taskid=${taskid}`);
  return null;
}

export async function getGuide(targetJpeg: ArrayBuffer, currentJpeg: ArrayBuffer, taskId: string,
                                filesDir?: string): Promise<GuideData | null> {
  Logger.info(`ARDA3-FLOW Da3.getGuide start target=${targetJpeg.byteLength}B current=${currentJpeg.byteLength}B task_id=${taskId}`);
  const url: string = cfgGuideInferUrl();
  // 关键修复:da3 demo 的 data 字段是 { task_id: this.taskId },与 generate 同会话。
  // 之前传 '{}' 导致服务端 os.path.join(None, ...) 崩。
  // ArkTS 严格禁裸 object literal,直接拼字符串(taskId 是 UUID,无需转义)。
  const dataField: string = '{"task_id":"' + taskId + '"}';
  Logger.info(`${TAG} getGuide infer data=${dataField}`);
  const parts: MultipartPart[] = [
    { name: 'files', filename: 'target_' + nowMs().toString() + '.jpg', contentType: 'image/jpeg', bytes: targetJpeg },
    { name: 'files', filename: 'current_' + nowMs().toString() + '.jpg', contentType: 'image/jpeg', bytes: currentJpeg },
    { name: 'data', text: dataField }
  ];
  // === 旁路存档:发出去的两张图 + 请求参数 ===
  const dbgDir: string | undefined = dbgEnsure(filesDir);
  const ts: string = dbgTs();
  dbgSaveBytes(dbgDir, `${ts}_guide_target.jpg`, targetJpeg);
  dbgSaveBytes(dbgDir, `${ts}_guide_current.jpg`, currentJpeg);
  dbgSaveText(dbgDir, `${ts}_guide_req.txt`,
    `url=${url}\ntask_id=${taskId}\ndata_field=${dataField}\ntarget_bytes=${targetJpeg.byteLength}\ncurrent_bytes=${currentJpeg.byteLength}\n`);
  const resp: HttpResult | null = await postMultipart(url, parts);
  if (resp === null) {
    Logger.error(`${TAG} infer POST returned null`);
    dbgSaveText(dbgDir, `${ts}_guide_resp.txt`, `STATUS=infer POST returned null (network/host error)\n`);
    return null;
  }
  if (resp.code < 200 || resp.code >= 300) {
    Logger.error(`${TAG} infer POST bad code=${resp.code} body=${truncate(safeForLog(resp.body), 1200)}`);
    dbgSaveText(dbgDir, `${ts}_guide_resp.txt`,
      `STATUS=infer POST bad code\ncode=${resp.code}\nbody=${resp.body}\n`);
    return null;
  }
  const json: Record<string, ESObject> | null = safeParse(resp.body);
  const taskid: string | null = pickString(json, ['taskid', 'task_id', 'taskId']);
  if (taskid === null) {
    Logger.error(`${TAG} infer no taskid in body=${truncate(safeForLog(resp.body), 1200)}`);
    dbgSaveText(dbgDir, `${ts}_guide_resp.txt`,
      `STATUS=infer no taskid\nbody=${resp.body}\n`);
    return null;
  }
  Logger.info(`${TAG} infer taskid=${taskid}`);
  const guide: GuideData | null = await pollGuideTask(taskid);
  // 旁路存档:轮询结束后存最终响应(taskid + parsed guide / 或失败原因)
  let respText: string = `infer_taskid=${taskid}\nsession_task_id=${taskId}\n`;
  if (guide !== null) {
    respText += `STATUS=SUCCESS\nparsed_guide=${JSON.stringify(guide)}\n`;
  } else {
    respText += `STATUS=POLL_FAIL_OR_TIMEOUT\n`;
  }
  dbgSaveText(dbgDir, `${ts}_guide_resp.txt`, respText);
  return guide;
}

// ───────────────────────────────────────────────
//  健康检查：检测后端服务可达性
// ───────────────────────────────────────────────

export interface HealthCheckResult {
  generateOk: boolean;
  guideOk: boolean;
  generateError: string;
  guideError: string;
}

export async function checkBackendHealth(): Promise<HealthCheckResult> {
  const result: HealthCheckResult = {
    generateOk: false, guideOk: false,
    generateError: '', guideError: ''
  };

  const genUrl: string = getCloudConfig().generate.host;
  const guideUrl: string = `${getCloudConfig().guide.host}/health`;

  const check = (url: string): Promise<string> => {
    return new Promise<string>((resolve: (v: string) => void) => {
      const req: http.HttpRequest = http.createHttp();
      req.request(url, {
        method: http.RequestMethod.GET,
        connectTimeout: 5000,
        readTimeout: 5000
      }, (err: BusinessError, data: http.HttpResponse) => {
        try {
          if (err) {
            resolve(`${err.code}: ${err.message}`);
            return;
          }
          if (data.responseCode >= 500) {
            resolve(`HTTP ${data.responseCode}`);
            return;
          }
          resolve('');
        } finally {
          req.destroy();
        }
      });
    });
  };

  const results: string[] = await Promise.all([check(genUrl), check(guideUrl)]);
  const genErr: string = results[0];
  const guideErr: string = results[1];
  result.generateOk = genErr.length === 0;
  result.generateError = genErr;
  result.guideOk = guideErr.length === 0; 
  
  result.guideError = guideErr;
  Logger.info(`${TAG} healthCheck: generate=${result.generateOk}(${genErr}) guide=${result.guideOk}(${guideErr})`);
  return result;
}

// ───────────────────────────────────────────────
//  服务 C：图片美学打分 (score)
// ───────────────────────────────────────────────

/** 单维度评分结果 */
export interface ScoreCriterion {
  /** "Good" | "Medium" | "Poor" */
  level: string;
  /** 1-2 句简要说明 */
  reason: string;
}

/** scoreImage() 返回值 */
export interface ScoreResult {
  /** 综合美学分数 1-100 */
  totalScore: number;
  /** 13 个美学维度的逐项评估，key 为英文维度名 */
  criteria: Record<string, ScoreCriterion>;
}

// URL helper：打分接口由 CloudConfig.getScoreUrl() 统一拼接。
function cfgScoreUrl(): string { return getScoreUrl(); }

/**
 * 上传一张 JPEG，同步调用打分接口，返回 ScoreResult 或 null（失败不抛）。
 *
 * @param jpeg  待评分图片的 ArrayBuffer（JPEG/PNG 均可，建议 JPEG quality=92）
 */
export async function scoreImage(jpeg: ArrayBuffer): Promise<ScoreResult | null> {
  const url: string = cfgScoreUrl();
  const ts: number = nowMs();
  Logger.info(`${TAG} scoreImage start url=${url} size=${jpeg.byteLength}`);

  // 单字段 multipart：image
  const parts: MultipartPart[] = [
    {
      name: 'image',
      bytes: jpeg,
      filename: 'photo.jpg',
      contentType: 'image/jpeg',
    }
  ];

  const resp: HttpResult | null = await postMultipart(url, parts);
  const cost: number = nowMs() - ts;
  if (resp === null) {
    Logger.warn(`${TAG} scoreImage failed (null response) cost=${cost}ms`);
    return null;
  }
  if (resp.code >= 400) {
    Logger.warn(`${TAG} scoreImage HTTP ${resp.code} cost=${cost}ms body=${truncate(safeForLog(resp.body), 200)}`);
    return null;
  }

  Logger.info(`${TAG} scoreImage resp cost=${cost}ms body=${truncate(safeForLog(resp.body), 300)}`);

  // 解析响应 JSON
  const root: Record<string, ESObject> | null = safeParse(resp.body);
  if (root === null) {
    Logger.warn(`${TAG} scoreImage JSON parse failed`);
    return null;
  }

  // 提取 total_score
  const totalScore: number | null = pickNumber(root, ['total_score']);
  if (totalScore === null) {
    Logger.warn(`${TAG} scoreImage missing total_score`);
    return null;
  }

  // 提取 criteria 对象
  const criteriaRaw: Record<string, ESObject> | null = pickObject(root, 'criteria');
  if (criteriaRaw === null) {
    Logger.warn(`${TAG} scoreImage missing criteria`);
    return null;
  }

  // 把 criteria object 转为 Record<string, ScoreCriterion>
  const criteria: Record<string, ScoreCriterion> = {};
  for (const key of Object.keys(criteriaRaw)) {
    const item: Record<string, ESObject> | null = pickObject(criteriaRaw, key);
    const level: string | null = pickString(item, ['level']);
    const reason: string | null = pickString(item, ['reason']);
    criteria[key] = {
      level: level !== null ? level : '',
      reason: reason !== null ? reason : '',
    };
  }

  const result: ScoreResult = {
    totalScore: Math.round(totalScore),
    criteria: criteria,
  };
  Logger.info(`${TAG} scoreImage success totalScore=${result.totalScore} dims=${Object.keys(criteria).length}`);
  return result;
}
