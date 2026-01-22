import 'package:flutter/material.dart';

import 'pages/servo_page.dart';
import 'pages/camera_page.dart';
import 'pages/python_page.dart'; // ✅ 新增
import 'pages/wifi_page.dart';

import 'api/esp_api.dart';
import 'config.dart';
import 'net/host_resolver.dart';
import 'net/wifi_info.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  await WifiInfo.initBootSsid();

  final r = await HostResolver.autoSelectHostEx();
  await ApiConfig.setHost(r.host, reason: r.reason);

  print("[BOOT] host=${ApiConfig.host}, via=${ApiConfig.hostReason}");

  runApp(const ESP32ControlApp());

  Future.delayed(const Duration(seconds: 1), () {
    WsControlApi.ensureConnect();
  });
}

class ESP32ControlApp extends StatelessWidget {
  const ESP32ControlApp({super.key});

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      debugShowCheckedModeBanner: false,
      home: MainLayout(),
    );
  }
}

class MainLayout extends StatefulWidget {
  const MainLayout({super.key});
  @override
  State<MainLayout> createState() => _MainLayoutState();
}

class _MainLayoutState extends State<MainLayout> {
  int _selectedIndex = 0;

  // ✅ pages 數量 = nav items 數量
  final List<Widget> _pages = const [
    ServoPage(),
    CameraPage(),
    PythonPage(),
    WiFiPage(),
  ];

  @override
  Widget build(BuildContext context) {
    final isMobile = MediaQuery.of(context).size.width < 740;

    return Scaffold(
      appBar: AppBar(
        title: Text("ESP32 控制面板 - ${ApiConfig.host} (${ApiConfig.hostReason})"),
      ),
      body: Row(
        children: [
          if (!isMobile) buildSidebar(),
          Expanded(child: _pages[_selectedIndex]),
        ],
      ),
      bottomNavigationBar: isMobile ? buildBottomBar() : null,
    );
  }

  /// ✅ 手機版底部 Bar（四格）
  Widget buildBottomBar() => BottomNavigationBar(
        currentIndex: _selectedIndex,
        onTap: (i) => setState(() => _selectedIndex = i),
        type: BottomNavigationBarType.fixed, // ✅ 四格建議加這行
        items: const [
          BottomNavigationBarItem(
            icon: Icon(Icons.settings_remote),
            label: "Servo",
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.camera_alt),
            label: "Camera",
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.computer),
            label: "Python",
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.wifi),
            label: "WiFi",
          ),
        ],
      );

  /// ✅ 桌面版側邊欄（四格）
  Widget buildSidebar() => NavigationRail(
        selectedIndex: _selectedIndex,
        onDestinationSelected: (i) => setState(() => _selectedIndex = i),
        labelType: NavigationRailLabelType.all,
        destinations: const [
          NavigationRailDestination(
            icon: Icon(Icons.settings_remote),
            label: Text("Servo"),
          ),
          NavigationRailDestination(
            icon: Icon(Icons.camera_alt),
            label: Text("Camera"),
          ),
          NavigationRailDestination(
            icon: Icon(Icons.computer),
            label: Text("Python"),
          ),
          NavigationRailDestination(
            icon: Icon(Icons.wifi),
            label: Text("WiFi"),
          ),
        ],
      );
}
