import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:excel/excel.dart' hide Border;
import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

import '../api/esp_api.dart';
import '../ui/ui_card.dart';

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

  final Excel excel = Excel.createExcel();
  late final Sheet sheet;

  late final ScrollController _verticalController;
  late final ScrollController _horizontalController;

  @override
  void initState() {
    super.initState();

    _verticalController = ScrollController();
    _horizontalController = ScrollController();

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

      final int seq = (data["seq"] is num) ? (data["seq"] as num).toInt() : -1;

      // 只有 seq 遞增才記錄
      if (lastSeq != null && seq <= lastSeq!) return;
      lastSeq = seq;

      final rawTarget = data["target"];
      final rawActual = data["actual"];
      final rawError = data["error"];

      if (rawTarget is! List || rawActual is! List || rawError is! List) {
        return;
      }

      late final List<double> t;
      late final List<double> a;
      late final List<double> e;

      try {
        t = rawTarget.map((v) => (v as num).toDouble()).toList();
        a = rawActual.map((v) => (v as num).toDouble()).toList();
        e = rawError.map((v) => (v as num).toDouble()).toList();
      } catch (_) {
        return;
      }

      final int len = [t.length, a.length, e.length].reduce(
        (x, y) => x < y ? x : y,
      );

      if (len <= 0) return;

      final now = DateTime.now().toIso8601String();

      for (int i = 0; i < len; i++) {
        sheet.appendRow([
          IntCellValue(seq),
          TextCellValue(now),
          TextCellValue("CH${i + 1}"),
          DoubleCellValue(t[i]),
          DoubleCellValue(a[i]),
          DoubleCellValue(e[i]),
        ]);
      }

      if (!mounted) return;

      setState(() {
        target = t.take(len).toList();
        actual = a.take(len).toList();
        error = e.take(len).toList();
        logCount++;
      });
    });
  }

  @override
  void dispose() {
    sub?.cancel();
    _verticalController.dispose();
    _horizontalController.dispose();
    super.dispose();
  }

  Future<void> exportExcel() async {
    if (kIsWeb) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("Web 暫不支援匯出")),
      );
      return;
    }

    if (logCount <= 0) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("目前沒有可匯出的資料")),
      );
      return;
    }

    final bytes = excel.encode();
    if (bytes == null) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("Excel 產生失敗")),
      );
      return;
    }

    final filename = "servo_log_${DateTime.now().millisecondsSinceEpoch}.xlsx";

    try {
      final path = await FilePicker.platform.saveFile(
        dialogTitle: "儲存 Servo Log",
        fileName: filename,
        type: FileType.custom,
        allowedExtensions: ['xlsx'],
        bytes: Uint8List.fromList(bytes),
      );

      if (!mounted) return;

      if (path == null) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text("已取消儲存")),
        );
        return;
      }

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
    final int n = [target.length, actual.length, error.length].reduce(
      (a, b) => a < b ? a : b,
    );

    return UiCard(
      title: "Servo 狀態",
      minHeight: 360,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          SizedBox(
            height: 220,
            width: double.infinity,
            child: Container(
              decoration: BoxDecoration(
                borderRadius: BorderRadius.circular(8),
                border: Border.all(
                  color: Colors.black12,
                  width: 1,
                ),
              ),
              child: ClipRRect(
                borderRadius: BorderRadius.circular(8),
                child: Scrollbar(
                  controller: _verticalController,
                  thumbVisibility: true,
                  child: SingleChildScrollView(
                    controller: _verticalController,
                    scrollDirection: Axis.vertical,
                    child: Scrollbar(
                      controller: _horizontalController,
                      thumbVisibility: true,
                      notificationPredicate: (_) => false,
                      child: SingleChildScrollView(
                        controller: _horizontalController,
                        scrollDirection: Axis.horizontal,
                        child: ConstrainedBox(
                          constraints: const BoxConstraints(minWidth: 520),
                          child: DataTable(
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
                            rows: n == 0
                                ? const [
                                    DataRow(
                                      cells: [
                                        DataCell(Text("-")),
                                        DataCell(Text("-")),
                                        DataCell(Text("-")),
                                        DataCell(Text("-")),
                                      ],
                                    ),
                                  ]
                                : List.generate(n, (i) {
                                    return DataRow(
                                      cells: [
                                        DataCell(Text("CH${i + 1}")),
                                        DataCell(
                                          Text(target[i].toStringAsFixed(2)),
                                        ),
                                        DataCell(
                                          Text(actual[i].toStringAsFixed(2)),
                                        ),
                                        DataCell(
                                          Text(error[i].toStringAsFixed(2)),
                                        ),
                                      ],
                                    );
                                  }),
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ),
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
    );
  }
}
