import { startTone, stopTone, isToneRunning } from 'libentry.so';
import { hilog } from '@kit.PerformanceAnalysisKit';

const DOMAIN = 0x0001;
const TAG = 'SLH_Index';

@Entry
@Component
struct Index {
  @State message: string = 'G0 冒烟：OHAudio 正弦波';
  @State toneRunning: boolean = false;

  private toggleTone(): void {
    if (this.toneRunning) {
      stopTone();
      this.toneRunning = isToneRunning();
      this.message = '已停止';
      hilog.info(DOMAIN, TAG, '%{public}s', 'stopTone called');
      return;
    }
    const ret: number = startTone();
    this.toneRunning = isToneRunning();
    this.message = ret === 0 ? '正在播放 440Hz / 1 秒' : `启动失败，错误码 ${ret}`;
    hilog.info(DOMAIN, TAG, 'startTone ret=%{public}d', ret);
  }

  build() {
    Column({ space: 24 }) {
      Text(this.message)
        .fontSize(20)
        .fontWeight(FontWeight.Bold)
        .textAlign(TextAlign.Center)

      Button(this.toneRunning ? '停止' : '播放 1 秒正弦波')
        .width(220)
        .onClick(() => {
          this.toggleTone();
        })
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
  }
}
