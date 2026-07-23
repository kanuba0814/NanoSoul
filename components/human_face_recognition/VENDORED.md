# vendored: espressif/human_face_recognition 0.3.2

来源: https://github.com/espressif/esp-dl `models/human_face_recognition`
commit `f43b41fd533da882382f9cd3ef305c829c138189`（= 我们锁定的 human_face_detect 0.5.0 的发布 commit，API 与 managed esp-dl 3.3.0 对齐）。

**为什么不走组件注册表**：已发布的 0.3.2 归档 manifest 钉死 `human_face_detect ~0.4.1`，
与本仓库锁定的 0.5.0 版本求解无解（repo 里该 commit 的 manifest 已改成 ~0.5.0，但归档没重发）。
上游修发布后可删掉本目录、改回 `idf_component.yml` 注册表依赖。

改动（相对上游原样）：
- 删 `idf_component.yml`（避免再次卷入版本求解；依赖走 CMake `REQUIRES`）。
- 只保留 P4 的 MFN 模型（`models/p4/human_face_feat_mfn_s8_v1.espdl`，1.27MB）；
  MBF（3.4MB）与 S3 模型未拷。
- 已知上游 bug：`recognize()/enroll()` 多脸时 `std::max_element` 比较器方向反了，
  实际选到**最小**脸——调用方（components/vision）只传单脸列表规避。

模型部署（`CONFIG_HUMAN_FACE_FEAT_MODEL_IN_SDCARD=y`）：
把 `models/p4/human_face_feat_mfn_s8_v1.espdl` 拷到 SD 卡 **`model/p4/`** 目录下
（目录名由 sdkconfig.defaults 的 `CONFIG_HUMAN_FACE_FEAT_MODEL_SDCARD_DIR="model/p4"` 决定，
与现役 SD 卡的既有目录保持一致）。
