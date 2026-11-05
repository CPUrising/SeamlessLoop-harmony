/*
 * 最小验证界面（任务 6.1）：
 * - 把合成 fixture 从 rawfile 复制到应用沙箱固定路径，再交给 native 加载。
 *   设计上文件走「沙箱内固定路径」（design.md Open Questions 第 3 条），
 *   打进 rawfile 只是为了让真机无需 push 也能复现听测。
 * - 播放 / 暂停 / 停止，显示播放进度与累计循环次数。
 * 页面只做展示与转发，解码、循环与进度计算全在 native 侧。
 */
import {
  load,
  play,
  pause,
  stop,
  setLoopPoints,
  setLooping,
  getProgress,
  getLoopCount,
  getState,
  getFormat,
  on,
  off
} from 'libentry.so';
import { common } from '@kit.AbilityKit';
import { fileIo as fs } from '@kit.CoreFileKit';
import { hilog } from '@kit.PerformanceAnalysisKit';

const DOMAIN = 0x0001;
const TAG = 'SLH_Index';

const FIXTURE_NAME: string = 'intro_loop_44100_mono.wav';
// 合成 fixture 的循环体固定 2 秒（见 fixtures/manifest.json），
// 因此 LoopStart 由「总采样数 − 2 × 采样率」推出，不把 33075 写死在页面里。
const LOOP_SECONDS: number = 2;

const STATE_IDLE: number = 0;
const STATE_PLAYING: number = 2;

@Entry
@Component
struct Index {
  @State status: string = '准备中';
  @State fixturePath: string = '';
  @State progressSeconds: number = 0;
  @State loopCount: number = 0;
  @State sampleRate: number = 0;
  @State totalFrames: number = 0;
  @State playing: boolean = false;

  private timerId: number = -1;

  aboutToAppear(): void {
    on('loop', (count: number) => {
      this.loopCount = count;
    });
    this.timerId = setInterval(() => {
      this.refresh();
    }, 200);
    this.prepareAndLoad();
  }

  aboutToDisappear(): void {
    if (this.timerId >= 0) {
      clearInterval(this.timerId);
      this.timerId = -1;
    }
    off('loop');
  }

  // 把 rawfile 中的 fixture 复制到沙箱固定路径；已存在则直接复用。
  private copyFixture(): string {
    const ctx: common.UIAbilityContext = getContext(this) as common.UIAbilityContext;
    const target: string = `${ctx.filesDir}/${FIXTURE_NAME}`;
    if (fs.accessSync(target)) {
      return target;
    }
    const content: Uint8Array = ctx.resourceManager.getRawFileContentSync(FIXTURE_NAME);
    const file = fs.openSync(target, fs.OpenMode.READ_WRITE | fs.OpenMode.CREATE | fs.OpenMode.TRUNC);
    try {
      fs.writeSync(file.fd, content.buffer as ArrayBuffer);
    } finally {
      fs.closeSync(file);
    }
    return target;
  }

  private prepareAndLoad(): void {
    try {
      this.fixturePath = this.copyFixture();
    } catch (err) {
      this.status = `复制 fixture 失败：${err}`;
      hilog.error(DOMAIN, TAG, 'copy fixture failed: %{public}s', `${err}`);
      return;
    }

    if (!load(this.fixturePath)) {
      this.status = `加载失败：${this.fixturePath}`;
      hilog.error(DOMAIN, TAG, 'load failed: %{public}s', this.fixturePath);
      return;
    }

    const format = getFormat();
    this.sampleRate = format.sampleRate;
    this.totalFrames = format.totalFrames;
    const loopStart: number = format.totalFrames - LOOP_SECONDS * format.sampleRate;
    if (!setLoopPoints(loopStart, format.totalFrames)) {
      this.status = `循环点设置失败：${loopStart} → ${format.totalFrames}`;
      hilog.error(DOMAIN, TAG, 'setLoopPoints failed: %{public}d -> %{public}d', loopStart,
        format.totalFrames);
      return;
    }
    setLooping(true);

    this.progressSeconds = 0;
    this.loopCount = 0;
    this.playing = false;
    this.status = `${format.sampleRate}Hz / ${format.channels}ch / ${format.totalFrames} 采样`;
    hilog.info(DOMAIN, TAG, 'loaded %{public}s, loop %{public}d -> %{public}d', this.fixturePath,
      loopStart, format.totalFrames);
  }

  private refresh(): void {
    this.playing = getState() === STATE_PLAYING;
    if (this.sampleRate > 0) {
      this.progressSeconds = getProgress() / this.sampleRate;
    }
    this.loopCount = getLoopCount();
  }

  private togglePlay(): void {
    if (getState() === STATE_IDLE) {
      this.prepareAndLoad();
      if (getState() === STATE_IDLE) {
        return;
      }
    }
    if (getState() === STATE_PLAYING) {
      pause();
      this.playing = false;
      return;
    }
    if (!play()) {
      this.status = '启动播放失败';
      return;
    }
    this.playing = true;
  }

  private stopPlayback(): void {
    stop();
    this.playing = false;
    this.progressSeconds = 0;
    this.loopCount = 0;
  }

  build() {
    Column({ space: 16 }) {
      Text(this.status)
        .fontSize(18)
        .fontWeight(FontWeight.Bold)
        .textAlign(TextAlign.Center)

      Text(this.fixturePath)
        .fontSize(12)
        .fontColor(Color.Gray)
        .textAlign(TextAlign.Center)
        .maxLines(2)

      Text(`进度 ${this.progressSeconds.toFixed(2)} s / 循环 ${this.loopCount} 次`)
        .fontSize(16)

      Row({ space: 16 }) {
        Button(this.playing ? '暂停' : '播放')
          .width(120)
          .onClick(() => {
            this.togglePlay();
          })

        Button('停止')
          .width(120)
          .onClick(() => {
            this.stopPlayback();
          })
      }
    }
    .width('100%')
    .height('100%')
    .padding(24)
    .justifyContent(FlexAlign.Center)
  }
}
