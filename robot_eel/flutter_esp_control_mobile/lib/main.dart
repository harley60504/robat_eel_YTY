import 'package:flutter/material.dart';

import 'api/esp_api.dart';
import 'config.dart';
import 'net/host_resolver.dart';
import 'net/wifi_info.dart';
import 'pages/python_page.dart';
import 'pages/wifi_page.dart';
import 'ui/ui_layout.dart';
import 'widgets/camera_control.dart';
import 'widgets/camera_stream.dart';
import 'widgets/mode_switch.dart';
import 'widgets/motion_param.dart';
import 'widgets/servo_control_panel.dart';
import 'widgets/servo_table.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  ErrorWidget.builder = (details) {
    return Material(
      color: const Color(0xFF050607),
      child: Center(
        child: Container(
          margin: const EdgeInsets.all(16),
          padding: const EdgeInsets.all(16),
          decoration: BoxDecoration(
            color: const Color(0xFF12161C),
            border: Border.all(color: const Color(0xFFB3261E)),
            borderRadius: BorderRadius.circular(8),
          ),
          child: const Text(
            "畫面載入失敗，請查看 debug console。",
            style: TextStyle(color: Colors.white),
          ),
        ),
      ),
    );
  };

  runApp(const ESP32ControlApp());
}

class ESP32ControlApp extends StatelessWidget {
  const ESP32ControlApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        brightness: Brightness.dark,
        scaffoldBackgroundColor: const Color(0xFF050607),
        colorScheme: ColorScheme.fromSeed(
          seedColor: const Color(0xFF27B5FF),
          brightness: Brightness.dark,
        ),
        cardTheme: CardThemeData(
          color: const Color(0xFF12161C),
          elevation: 0,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(8),
            side: const BorderSide(color: Color(0xFF252B33)),
          ),
        ),
        appBarTheme: const AppBarTheme(
          backgroundColor: Color(0xFF090B0E),
          foregroundColor: Colors.white,
          elevation: 0,
        ),
        inputDecorationTheme: const InputDecorationTheme(
          filled: true,
          fillColor: Color(0xFF0B0F14),
        ),
      ),
      home: const MainLayout(),
    );
  }
}

class MainLayout extends StatefulWidget {
  const MainLayout({super.key});

  @override
  State<MainLayout> createState() => _MainLayoutState();
}

class _MainLayoutState extends State<MainLayout> {
  int mode = -1;
  int activeMenu = 0;
  bool userSelectedMenu = false;

  @override
  void initState() {
    super.initState();

    final cached = WsControlApi.lastCtrlParams;
    if (cached != null) {
      mode = cached['mode'] ?? -1;
      activeMenu = mode >= 0 && mode <= 3 ? mode : 0;
    }

    WsControlApi.ctrlParamsNotifier.addListener(_onCtrlParams);
    _bootAndConnect();
  }

  Future<void> _bootAndConnect() async {
    try {
      await WifiInfo.initBootSsid();

      final r = await HostResolver.autoSelectHostEx();
      await ApiConfig.setHost(r.host, reason: r.reason);

      debugPrint("[BOOT] host=${ApiConfig.host}, via=${ApiConfig.hostReason}");
      if (mounted) setState(() {});
    } catch (e) {
      debugPrint("[BOOT] fallback host=${ApiConfig.host}, error=$e");
    }

    Future.delayed(const Duration(seconds: 1), () {
      WsControlApi.ensureConnect();
    });
  }

  void _onCtrlParams() {
    final msg = WsControlApi.ctrlParamsNotifier.value;
    if (!mounted || msg == null) return;

    final newMode = msg['mode'] ?? -1;
    if (newMode != mode) {
      setState(() {
        mode = newMode;
        if (!userSelectedMenu && newMode >= 0 && newMode <= 3) {
          activeMenu = newMode;
        }
      });
    }
  }

  @override
  void dispose() {
    WsControlApi.ctrlParamsNotifier.removeListener(_onCtrlParams);
    super.dispose();
  }

  void openSettingsSheet() {
    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      builder: (context) {
        return DraggableScrollableSheet(
          expand: false,
          initialChildSize: 0.78,
          minChildSize: 0.36,
          maxChildSize: 0.92,
          builder: (context, scrollController) {
            return Container(
              decoration: const BoxDecoration(
                color: Color(0xFF0B0F14),
                borderRadius: BorderRadius.vertical(top: Radius.circular(18)),
              ),
              child: Center(
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 460),
                  child: ListView(
                    controller: scrollController,
                    padding: const EdgeInsets.fromLTRB(14, 8, 14, 18),
                    children: [
                      Center(
                        child: Container(
                          width: 44,
                          height: 4,
                          margin: const EdgeInsets.only(bottom: 12),
                          decoration: BoxDecoration(
                            color: Colors.white24,
                            borderRadius: BorderRadius.circular(99),
                          ),
                        ),
                      ),
                      const _SettingsSheet(),
                    ],
                  ),
                ),
              ),
            );
          },
        );
      },
    );
  }

  Widget buildActiveControl({bool fillHeight = false}) {
    final Widget child;
    if (activeMenu == 4) {
      child = PythonPage(compact: true, fillHeight: fillHeight);
    } else if (activeMenu == 3) {
      child = const ServoControlPanel(compact: true);
    } else {
      child = const MotionParam(compact: true);
    }

    if (fillHeight) {
      return Expanded(
        child: activeMenu == 4 ? child : SingleChildScrollView(child: child),
      );
    }

    return child;
  }

  Widget buildControlPanel({bool fillHeight = false}) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        ModeSwitch(
          compact: true,
          selectedMode: activeMenu,
          onModeSelected: (m) {
            setState(() {
              activeMenu = m;
              userSelectedMenu = true;
            });
          },
        ),
        const SizedBox(height: UiLayout.cardGap),
        buildActiveControl(fillHeight: fillHeight),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    final isMobile =
        MediaQuery.of(context).size.width < UiLayout.mobileBreakpoint;

    return Scaffold(
      appBar: AppBar(
        title: Text("ESP32 控制面板 - ${ApiConfig.host} (${ApiConfig.hostReason})"),
        actions: [
          IconButton(
            tooltip: "設定",
            onPressed: openSettingsSheet,
            icon: const Icon(Icons.settings),
          ),
        ],
      ),
      body: Padding(
        padding: UiLayout.pagePadding,
        child: isMobile
            ? SingleChildScrollView(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    AspectRatio(
                      aspectRatio: 4 / 3,
                      child: CameraStreamWS(wsUrl: ApiConfig.wsStreamUrl),
                    ),
                    const SizedBox(height: UiLayout.cardGap),
                    buildControlPanel(),
                  ],
                ),
              )
            : LayoutBuilder(
                builder: (context, constraints) {
                  final sideWidth = (constraints.maxWidth * 0.24)
                      .clamp(320.0, 430.0)
                      .toDouble();
                  final gap = constraints.maxWidth < 1100 ? 12.0 : UiLayout.gap;

                  return Row(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Expanded(
                        child: CameraStreamWS(wsUrl: ApiConfig.wsStreamUrl),
                      ),
                      SizedBox(width: gap),
                      SizedBox(
                        width: sideWidth,
                        child: buildControlPanel(fillHeight: true),
                      ),
                    ],
                  );
                },
              ),
      ),
    );
  }
}

class _SettingsSheet extends StatefulWidget {
  const _SettingsSheet();

  @override
  State<_SettingsSheet> createState() => _SettingsSheetState();
}

class _SettingsSheetState extends State<_SettingsSheet> {
  int selected = 0;

  @override
  Widget build(BuildContext context) {
    final pages = const [
      WiFiPage(compact: true),
      CameraControlPanel(compact: true, embedded: true),
      ServoTable(compact: true),
    ];

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          children: [
            Expanded(
              child: Text(
                "設定",
                style: Theme.of(context).textTheme.titleLarge,
              ),
            ),
            IconButton(
              tooltip: "關閉",
              onPressed: () => Navigator.pop(context),
              icon: const Icon(Icons.close),
            ),
          ],
        ),
        const SizedBox(height: 8),
        SegmentedButton<int>(
          segments: const [
            ButtonSegment(
              value: 0,
              icon: Icon(Icons.wifi),
              label: Text("Wi-Fi"),
            ),
            ButtonSegment(
              value: 1,
              icon: Icon(Icons.tune),
              label: Text("畫質"),
            ),
            ButtonSegment(
              value: 2,
              icon: Icon(Icons.table_chart),
              label: Text("Servo"),
            ),
          ],
          selected: {selected},
          onSelectionChanged: (value) {
            setState(() => selected = value.first);
          },
          showSelectedIcon: false,
        ),
        const SizedBox(height: 12),
        pages[selected],
      ],
    );
  }
}
