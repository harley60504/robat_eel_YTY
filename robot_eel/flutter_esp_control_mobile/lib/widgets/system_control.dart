import 'dart:typed_data';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';

import '../api/esp_http_api.dart';
import '../ui/ui_card.dart';

class SystemControl extends StatefulWidget {
  const SystemControl({super.key});

  @override
  State<SystemControl> createState() => _SystemControlState();
}

class _SystemControlState extends State<SystemControl> {
  bool downloading = false;

  Future<void> downloadBoardCsv() async {
    if (downloading) return;

    setState(() => downloading = true);
    try {
      final bytes = await EspHttpApi.downloadBoardCsv();
      final filename = "board_data_${DateTime.now().millisecondsSinceEpoch}.csv";
      final path = await FilePicker.platform.saveFile(
        dialogTitle: "Save board CSV",
        fileName: filename,
        type: FileType.custom,
        allowedExtensions: const ['csv'],
        bytes: Uint8List.fromList(bytes),
      );

      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(path == null ? "CSV download canceled" : "Saved CSV: $path"),
        ),
      );
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text("Board CSV download failed: $e")),
      );
    } finally {
      if (mounted) setState(() => downloading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return UiCard(
      title: "System Control",
      child: Padding(
        padding: EdgeInsets.zero,
        child: Row(
          children: [
            ElevatedButton(onPressed: () {}, child: const Text("Pause / Resume")),
            const SizedBox(width: 12),
            ElevatedButton(
              onPressed: downloading ? null : downloadBoardCsv,
              child: Text(downloading ? "Downloading..." : "Download board CSV"),
            ),
          ],
        ),
      ),
    );
  }
}
