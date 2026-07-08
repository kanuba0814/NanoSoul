// 史莱姆外观参考件 —— 半透外壳 + 分色内件合成预览。
// 中间件 STL 由 nanosoul_enclosure.py 生成在 .preview/ 下。
// 渲染（前 3/4 与正面两视角）：
//   openscad -o ../enclosure_preview.png --imgsize=1180,1000 --colorscheme=Tomorrow \
//     --camera=290,-120,135,0,0,33 --projection=p preview.scad
//   openscad -o ../enclosure_face.png   --imgsize=950,1000 --colorscheme=Tomorrow \
//     --camera=340,0,78,0,0,33 --projection=p preview.scad
$fn = 80;
color([0.42, 0.85, 0.52, 0.20]) import(".preview/shell.stl");   // 半透外壳
color([0.13, 0.30, 0.62])       import(".preview/elec.stl");    // 载板 + 开发板
color([0.92, 0.78, 0.18])       import(".preview/batt.stl");    // 2×18650
color([0.08, 0.09, 0.12])       import(".preview/face.stl");    // 屏(脸) + 相机
color([0.95, 0.36, 0.36])       import(".preview/led.stl");     // 双腮状态 LED
color([0.22, 0.22, 0.24])       import(".preview/wheel.stl");   // 3× 电机轮
color([0.30, 0.75, 0.95])       import(".preview/port.stl");    // 光窗/麦/喇叭/键/USB
