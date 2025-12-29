import 'package:flutter/material.dart';
import '../api/esp_api.dart';

class ServoTable extends StatefulWidget {
  const ServoTable({super.key});

  @override
  State<ServoTable> createState() => _ServoTableState();
}

class _ServoTableState extends State<ServoTable> {
  int count = 0;
  List<double> target = [];
  List<double> actual = [];
  List<double> error = [];

  @override
  void initState() {
    super.initState();

    WsControlApi.stream().listen((msg) {
      if (msg["type"] == "servo_status") {
        setState(() {
          count = msg["count"] ?? 0;

          target = (msg["target"] as List)
              .map((e) => (e as num).toDouble())
              .toList();
          actual = (msg["actual"] as List)
              .map((e) => (e as num).toDouble())
              .toList();
          error = (msg["error"] as List)
              .map((e) => (e as num).toDouble())
              .toList();
        });
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    List<DataRow> rows = [];

    for (int i = 0; i < count; i++) {
      rows.add(
        DataRow(
          cells: [
            DataCell(Text("${i + 1}")),
            DataCell(Text(target[i].toStringAsFixed(2))),
            DataCell(Text(actual[i].toStringAsFixed(2))),
            DataCell(Text(error[i].toStringAsFixed(2))),
          ],
        ),
      );
    }

    return Card(
      elevation: 3,

      child: SizedBox(
        width: 600,

        child: DataTable(
          columns: const [
            DataColumn(label: Text("ID")),
            DataColumn(label: Text("Target (deg)")),
            DataColumn(label: Text("Actual (deg)")),
            DataColumn(label: Text("Error (deg)")),
          ],
          rows: rows,
        ),
      ),
    );
  }
}
