import 'package:flutter/material.dart';
import '../widgets/wifi_status_card.dart';
import '../widgets/wifi_scan.dart';

class WiFiPage extends StatelessWidget {
  const WiFiPage({super.key});

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.of(context).size.width;
    final isMobile = width < 700;

    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 1200),
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(12),
          child: isMobile
              ? Column(
                  children: const [
                    WiFiStatusCard(),
                    SizedBox(height: 12),
                    WiFiScanCard(),
                  ],
                )
              : Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: const [
                    Expanded(child: WiFiStatusCard()),
                    SizedBox(width: 12),
                    Expanded(child: WiFiScanCard()),
                  ],
                ),
        ),
      ),
    );
  }
}
