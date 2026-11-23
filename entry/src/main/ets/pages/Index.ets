/*
 * 最小验证界面（任务 6.1）：
 * - 把合成 fixture 从 rawfile 复制到应用沙箱固定路径，再交给 native 加载。
 *   设计上文件走「沙箱内固定路径」（design.md Open Questions 第 3 条），
 *   打进 rawfile 只是为了让真机无需 push 也能复现听测。
 * - 支持两种曲目：单文件 WAV，以及 A/B 双文件（按 A 侧路径加载，native 自动配对）。
 * - 播放 / 暂停 / 停止，显示播放进度与累计循环次数。
 * 页面只做展示与转发，解码、配对、循环与进度计算全在 native 侧。
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

// 单文件曲目：循环体固定 2 秒（见 fixtures/manifest.json）。
const SINGLE_FIXTURE: string = 'intro_loop_44100_mono.wav';
const SINGLE_LOOP_SECONDS: number = 2;

// A/B 曲目：A 段 33075 帧（0.75 秒），B 段 88200 帧（2 秒）。
// 循环点按虚拟时间轴表示：LoopStart = A 的帧数，LoopEnd = 虚拟轴总长。
const PAIR_FIXTURE_A: string = 'ab_pair_44100_stereo_a.wav';
const PAIR_FIXTURE_B: string = 'ab_pair_44100_stereo_b.wav';
const PAIR_A_SECONDS: number = 0.75;

const STATE_IDLE: number = 0;
const STATE_PLAYING: number = 2;

@Entry
@Component
struct Index {
  @State status: string = '准备中';
  @State fixturePath: string = '';
  @State progressSeconds: number = 0;
  @State loopCount: number = 0;
  @State underrunBytes: number = 0;
  @State loopStart: number = 0;
  @State sampleRate: number = 0;
  @State totalFrames: number = 0;
  @State playing: boolean = false;
  @State isPair: boolean = false;
  @State usePair: boolean = false;

  private timerId: number = -1;

  aboutToAppear(): void {
    on('loop', (count: number) => {
      this.loopCount = count;
    });
    // T17 第 3 条：欠载事件端到端观测，累计静音字节数。
    on('underrun', (bytes: number) => {
      this.underrunBytes += bytes;
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
    off('underrun');
  }

  // 把 rawfile 中的 fixture 复制到沙箱固定路径；已存在则直接复用。
  private copyFixture(name: string): string {
    const ctx: common.UIAbilityContext = getContext(this) as common.UIAbilityContext;
    const target: string = `${ctx.filesDir}/${name}`;
    if (fs.accessSync(target)) {
      return target;
    }
    const content: Uint8Array = ctx.resourceManager.getRawFileContentSync(name);
    const file = fs.openSync(target, fs.OpenMode.READ_WRITE | fs.OpenMode.CREATE | fs.OpenMode.TRUNC);
    try {
      fs.writeSync(file.fd, content.buffer as ArrayBuffer);
    } finally {
      fs.closeSync(file);
    }
    return target;
  }

  private prepareAndLoad(): void {
    let path: string = '';
    try {
      if (this.usePair) {
        // A/B 模式：两个文件都要落到沙箱，native 按 A 侧路径自动配对。
        this.copyFixture(PAIR_FIXTURE_B);
        path = this.copyFixture(PAIR_FIXTURE_A);
      } else {
        path = this.copyFixture(SINGLE_FIXTURE);
      }
    } catch (err) {
      this.status = `复制 fixture 失败：${err}`;
      hilog.error(DOMAIN, TAG, 'copy fixture failed: %{public}s', `${err}`);
      return;
    }
    this.fixturePath = path;

    if (!load(this.fixturePath)) {
      this.status = `加载失败：${this.fixturePath}`;
      hilog.error(DOMAIN, TAG, 'load failed: %{public}s', this.fixturePath);
      return;
    }

    const format = getFormat();
    this.sampleRate = format.sampleRate;
    this.totalFrames = format.totalFrames;
    this.isPair = format.isPair;

    // 循环点按虚拟时间轴计算：A/B 模式从 B 的第 0 帧开始循环，
    // 单文件模式保留最后 2 秒作为循环体。
    const loopStart: number = this.isPair
      ? Math.round(PAIR_A_SECONDS * format.sampleRate)
      : format.totalFrames - SINGLE_LOOP_SECONDS * format.sampleRate;
    this.loopStart = loopStart;
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
    const kind: string = this.isPair ? 'A/B 双文件' : '单文件';
    this.status = `${kind} / ${format.sampleRate}Hz / ${format.channels}ch / ${format.totalFrames} 采样`;
    hilog.info(DOMAIN, TAG, 'loaded %{public}s, pair=%{public}d, loop %{public}d -> %{public}d',
      this.fixturePath, this.isPair ? 1 : 0, loopStart, format.totalFrames);
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

  // 在单文件与 A/B 双文件之间切换曲目，切换后重新加载。
  private switchTrack(): void {
    stop();
    this.usePair = !this.usePair;
    this.underrunBytes = 0;
    this.prepareAndLoad();
  }

  // 任务 6.3：播放中把循环起点在「原值」与「原值 + 1 秒」之间切换，
  // 用于听测改点是否产生明显中断（T05 的 200ms 条款）。
  private shiftLoopStart(): void {
    if (this.sampleRate <= 0 || this.totalFrames <= 0) {
      return;
    }
    const base: number = this.isPair
      ? Math.round(PAIR_A_SECONDS * this.sampleRate)
      : this.totalFrames - SINGLE_LOOP_SECONDS * this.sampleRate;
    const shifted: number = base + this.sampleRate;
    const next: number = this.loopStart === base ? shifted : base;
    if (!setLoopPoints(next, this.totalFrames)) {
      this.status = `改点失败：${next} → ${this.totalFrames}`;
      return;
    }
    this.loopStart = next;
    this.status = `循环点 ${next} → ${this.totalFrames}`;
    hilog.info(DOMAIN, TAG, 'loop points changed to %{public}d', next);
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

      Text(`欠载 ${this.underrunBytes} 字节`)
        .fontSize(14)
        .fontColor(this.underrunBytes === 0 ? Color.Gray : Color.Red)

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

      Button(this.usePair ? '切到单文件' : '切到 A/B 双文件')
        .width(256)
        .onClick(() => {
          this.switchTrack();
        })

      Button('改循环点')
        .width(256)
        .onClick(() => {
          this.shiftLoopStart();
        })
    }
    .width('100%')
    .height('100%')
    .padding(24)
    .justifyContent(FlexAlign.Center)
  }
}
