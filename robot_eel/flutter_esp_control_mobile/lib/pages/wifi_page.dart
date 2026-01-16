import 'package:flutter/material.dart';
import '../widgets/wifi_status_card.dart';
import '../widgets/wifi_scan.dart';
import '../ui/ui_layout.dart';

class WiFiPage extends StatelessWidget {
  const WiFiPage({super.key});

  @override
  Widget build(BuildContext context) {
    final width = MediaQuery.of(context).size.width;
    final isMobile = width < UiLayout.mobileBreakpoint;

    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: UiLayout.pageMaxWidth),
        child: Padding(
          padding: UiLayout.pagePadding,
          child: isMobile
              ? ListView(
                  children: const [
                    WiFiStatusCard(),
                    SizedBox(height: UiLayout.gap),
                    WiFiScanCard(),
                  ],
                )
              : Row(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: const [
                    Expanded(child: WiFiStatusCard()),
                    SizedBox(width: UiLayout.gap),
                    Expanded(child: WiFiScanCard()),
                  ],
                ),
        ),
      ),
    );
  }
}
