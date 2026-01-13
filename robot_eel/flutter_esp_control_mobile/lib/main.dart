import 'package:flutter/material.dart';

import 'pages/dashboard_page.dart';
import 'pages/camera_page.dart';
import 'pages/wifi_page.dart';

import 'api/esp_api.dart';
import 'config.dart';
import 'net/host_resolver.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  final host = await HostResolver.autoSelectHost();
  await ApiConfig.setHost(host);

  runApp(const ESP32ControlApp());

  // ✅ 確保 WS 啟動
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

  final List<Widget> _pages = const [DashboardPage(), CameraPage(), WiFiPage()];

  @override
  Widget build(BuildContext context) {
    final isMobile = MediaQuery.of(context).size.width < 740;

    return Scaffold(
      appBar: AppBar(title: Text("ESP32 控制面板 - ${ApiConfig.host}")),
      body: Row(
        children: [
          if (!isMobile) buildSidebar(),
          Expanded(child: _pages[_selectedIndex]),
        ],
      ),
      bottomNavigationBar: isMobile ? buildBottomBar() : null,
    );
  }

  Widget buildBottomBar() => BottomNavigationBar(
    currentIndex: _selectedIndex,
    onTap: (i) => setState(() => _selectedIndex = i),
    items: const [
      BottomNavigationBarItem(icon: Icon(Icons.dashboard), label: "Dashboard"),
      BottomNavigationBarItem(icon: Icon(Icons.camera_alt), label: "Camera"),
      BottomNavigationBarItem(icon: Icon(Icons.wifi), label: "WiFi"),
    ],
  );

  Widget buildSidebar() => NavigationRail(
    selectedIndex: _selectedIndex,
    onDestinationSelected: (i) => setState(() => _selectedIndex = i),
    labelType: NavigationRailLabelType.all,
    destinations: const [
      NavigationRailDestination(
        icon: Icon(Icons.dashboard),
        label: Text("Dashboard"),
      ),
      NavigationRailDestination(
        icon: Icon(Icons.camera_alt),
        label: Text("Camera"),
      ),
      NavigationRailDestination(icon: Icon(Icons.wifi), label: Text("WiFi")),
    ],
  );
}
