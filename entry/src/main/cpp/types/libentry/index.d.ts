/**
 * libentry.so 的 TypeScript 声明。
 * 与 cpp/napi_init.cpp 暴露的接口一一对应。
 */

/** 音频格式信息。 */
export interface AudioFormat {
  sampleRate: number;
  channels: number;
  totalFrames: number;
  /** 是否按 A/B 双文件（虚拟时间轴）加载。 */
  isPair: boolean;
}

/** 事件名。 */
export type EventName = 'loop' | 'end' | 'underrun';

/** 加载音频文件。成功返回 true。 */
export const load: (path: string) => boolean;

/** 开始或恢复播放。 */
export const play: () => boolean;

/** 暂停，保留播放位置。 */
export const pause: () => void;

/** 停止并回到文件开头。 */
export const stop: () => void;

/** 按采样点跳转。 */
export const seekToSample: (frame: number) => boolean;

/** 设置循环区间（采样点）。 */
export const setLoopPoints: (startFrame: number, endFrame: number) => boolean;

/** 开关循环。 */
export const setLooping: (enabled: boolean) => void;

/** 当前播放位置（采样点，已做循环回绕补偿）。 */
export const getProgress: () => number;

/** 累计循环次数。 */
export const getLoopCount: () => number;

/** 当前播放状态：0=idle 1=ready 2=playing 3=paused。 */
export const getState: () => number;

/** 当前音频格式。 */
export const getFormat: () => AudioFormat;

/**
 * 订阅事件。
 * - loop：参数为累计循环次数
 * - end：无参数
 * - underrun：参数为本次静音字节数
 */
export const on: (event: EventName, callback: (value: number) => void) => boolean;

/** 取消订阅。 */
export const off: (event: EventName) => void;

/** G0 冒烟：播放 1 秒 440Hz 正弦波。 */
export const startTone: () => number;
export const stopTone: () => void;
export const isToneRunning: () => boolean;
