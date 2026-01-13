import 'dart:async';
import 'dart:io';

import 'package:excel/excel.dart';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';

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
      if (msg is! Map) return;
      if (!mounted) return;
      if (msg["type"] != "servo_status") return;

      final int seq = msg["seq"] ?? -1;
      if (lastSeq == seq) return; // 避免重複
      lastSeq = seq;

      final t = (msg["target"] as List)
          .map((e) => (e as num).toDouble())
          .toList();
      final a = (msg["actual"] as List)
          .map((e) => (e as num).toDouble())
          .toList();
      final e = (msg["error"] as List)
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

  /// ✅ Android / iOS 匯出：存成檔案
  Future<void> exportExcel() async {
    final bytes = excel.encode();
    if (bytes == null) return;

    final dir = await getApplicationDocumentsDirectory();
    final file = File('${dir.path}/servo_log.xlsx');
    await file.writeAsBytes(bytes, flush: true);

    if (!mounted) return;

    ScaffoldMessenger.of(
      context,
    ).showSnackBar(SnackBar(content: Text('已匯出至：${file.path}')));
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
                  onPressed: exportExcel,
                  child: const Text("匯出 Excel"),
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
