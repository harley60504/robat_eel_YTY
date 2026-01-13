import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:excel/excel.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'package:share_plus/share_plus.dart';

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

      // msg 可能是 json string，也可能已經是 map，這裡做保護
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

      final t = (data["target"] as List)
          .map((e) => (e as num).toDouble())
          .toList();
      final a = (data["actual"] as List)
          .map((e) => (e as num).toDouble())
          .toList();
      final e = (data["error"] as List)
          .map((e) => (e as num).toDouble())
          .toList();

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

  /// ✅ 匯出 Excel → 直接分享 → 分享後刪除，不佔空間
  /// ✅ Web 會直接禁用，不執行
  Future<void> exportExcel() async {
    if (kIsWeb) return; // Web 不支援 dart:io / temp directory

    final bytes = excel.encode();
    if (bytes == null) return;

    // ✅ 存到暫存資料夾（temp），避免占空間
    final dir = await getTemporaryDirectory();

    // ✅ 每次不同檔名，避免覆蓋/快取問題
    final filename = "servo_log_${DateTime.now().millisecondsSinceEpoch}.xlsx";
    final file = File('${dir.path}/$filename');

    await file.writeAsBytes(bytes, flush: true);

    if (!mounted) return;

    try {
      await Share.shareXFiles([XFile(file.path)], text: "Servo log 匯出");
    } finally {
      // ✅ 分享完刪除，避免佔空間
      if (await file.exists()) {
        await file.delete();
      }
    }

    if (!mounted) return;

    ScaffoldMessenger.of(
      context,
    ).showSnackBar(const SnackBar(content: Text("已分享 Excel（不保留檔案）")));
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

            SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              child: DataTable(
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

            const SizedBox(height: 12),

            Row(
              children: [
                ElevatedButton(
                  onPressed: kIsWeb ? null : exportExcel,
                  child: Text(kIsWeb ? "Web 不支援匯出" : "匯出並分享"),
                ),
                const SizedBox(width: 12),
                Text("已記錄 $logCount 筆"),
                const SizedBox(width: 12),
                Text("最新 Seq = ${lastSeq ?? '-'}"),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
