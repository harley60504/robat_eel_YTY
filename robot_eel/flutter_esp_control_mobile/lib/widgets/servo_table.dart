import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:excel/excel.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../api/esp_api.dart';

class ServoTable extends StatefulWidget {
  const ServoTable({super.key});

  @override
  State<ServoTable> createState() => _ServoTableState();
}

class _ServoTableState extends State<ServoTable> {
  List<double> target = [];
  List<double> actual = [];
  List<double> error = [];

  int? lastSeq;
  int logCount = 0;

  StreamSubscription? sub;

  final excel = Excel.createExcel();
  late final Sheet sheet;

  @override
  void initState() {
    super.initState();

    sheet = excel['ServoLog'];
    sheet.appendRow([
      TextCellValue("Seq"),
      TextCellValue("Time"),
      TextCellValue("Channel"),
      TextCellValue("Target (deg)"),
      TextCellValue("Actual (deg)"),
      TextCellValue("Error (deg)"),
    ]);

    sub = WsControlApi.stream().listen((msg) {
      if (!mounted) return;

      dynamic data = msg;

      if (msg is String) {
        try {
          data = jsonDecode(msg);
        } catch (_) {
          return;
        }
      }

      if (data is! Map) return;
      if (data["type"] != "servo_status") return;

      final int seq = data["seq"] ?? -1;
      if (lastSeq == seq) return; // 避免重複
      lastSeq = seq;

      final t =
          (data["target"] as List).map((e) => (e as num).toDouble()).toList();
      final a =
          (data["actual"] as List).map((e) => (e as num).toDouble()).toList();
      final e =
          (data["error"] as List).map((e) => (e as num).toDouble()).toList();

      final now = DateTime.now().toIso8601String();

      for (int i = 0; i < t.length; i++) {
        sheet.appendRow([
          IntCellValue(seq),
          TextCellValue(now),
          TextCellValue("CH${i + 1}"),
          DoubleCellValue(t[i]),
          DoubleCellValue(a[i]),
          DoubleCellValue(e[i]),
        ]);
      }

      setState(() {
        target = t;
        actual = a;
        error = e;
        logCount++;
      });
    });
  }

  @override
  void dispose() {
    sub?.cancel();
    super.dispose();
  }

  /// ✅ 匯出 Excel：讓使用者選擇儲存位置
  /// ✅ Android/iOS：必須用 bytes 存檔（不需要額外權限）
  /// ✅ Windows/macOS/Linux：可拿到 path（也可用 bytes）
  Future<void> exportExcel() async {
    if (kIsWeb) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("Web 暫不支援匯出")),
      );
      return;
    }

    final bytes = excel.encode();
    if (bytes == null) return;

    final filename = "servo_log_${DateTime.now().millisecondsSinceEpoch}.xlsx";

    try {
      final path = await FilePicker.platform.saveFile(
        dialogTitle: "儲存 Servo Log",
        fileName: filename,
        type: FileType.custom,
        allowedExtensions: ['xlsx'],

        // ✅ Android/iOS 必填：直接交給系統檔案選擇器儲存
        bytes: Uint8List.fromList(bytes),
      );

      // 使用者取消
      if (path == null) {
        if (!mounted) return;
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text("已取消儲存")),
        );
        return;
      }

      // ✅ 桌機有些情況會回傳 path，但 bytes 已經存好了
      // 這裡只做提示即可（不用再 File(path).writeAsBytes）
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text("已匯出：$path")),
      );
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text("匯出失敗：$e")),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final n = target.length;

    return Card(
      elevation: 3,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text("Servo 狀態", style: TextStyle(fontSize: 20)),
            const SizedBox(height: 12),

            /// ✅ 修正手機 Right Overflow 的關鍵：
            /// - 不要用 SizedBox(width: constraints.maxWidth)
            /// - 改用 minWidth，讓表格至少等於卡片寬，但可水平滾動擴張
            LayoutBuilder(
              builder: (context, constraints) {
                return ConstrainedBox(
                  constraints: const BoxConstraints(maxHeight: 260),
                  child: Scrollbar(
                    thumbVisibility: true,
                    child: SingleChildScrollView(
                      scrollDirection: Axis.vertical,
                      child: SingleChildScrollView(
                        scrollDirection: Axis.horizontal,
                        child: ConstrainedBox(
                          constraints: BoxConstraints(
                            minWidth: constraints.maxWidth,
                          ),
                          child: DataTable(
                            // ✅ 手機更不容易爆版
                            columnSpacing: 16,
                            horizontalMargin: 12,

                            headingRowHeight: 44,
                            dataRowMinHeight: 40,
                            dataRowMaxHeight: 40,
                            columns: const [
                              DataColumn(label: Text("CH")),
                              DataColumn(label: Text("Target (deg)")),
                              DataColumn(label: Text("Actual (deg)")),
                              DataColumn(label: Text("Error (deg)")),
                            ],
                            rows: List.generate(n, (i) {
                              return DataRow(
                                cells: [
                                  DataCell(Text("CH${i + 1}")),
                                  DataCell(Text(target[i].toStringAsFixed(2))),
                                  DataCell(Text(actual[i].toStringAsFixed(2))),
                                  DataCell(Text(error[i].toStringAsFixed(2))),
                                ],
                              );
                            }),
                          ),
                        ),
                      ),
                    ),
                  ),
                );
              },
            ),

            const SizedBox(height: 12),

            Wrap(
              spacing: 12,
              runSpacing: 8,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                ElevatedButton(
                  onPressed: exportExcel,
                  child: const Text("匯出 Excel"),
                ),
                Text("已記錄 $logCount 筆"),
                Text("最新 Seq = ${lastSeq ?? '-'}"),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
