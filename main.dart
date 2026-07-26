import 'package:flutter/material.dart';
import 'mileto_control_page.dart';

void main() {
  runApp(const MiletoKineticApp());
}

class MiletoKineticApp extends StatelessWidget {
  const MiletoKineticApp({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Mileto Kinetic Control',
      debugShowCheckedModeBanner: false,

      // Tema Escuro Profissional Premium para Eventos e Iluminação Cênica
      theme: ThemeData(
        brightness: Brightness.dark,
        primaryColor: Colors.black,
        scaffoldBackgroundColor: const Color(0xFF0F0F0F),
        colorScheme: const ColorScheme.dark(
          primary: Colors.amber,
          secondary: Colors.amberAccent,
          background: Color(0xFF0F0F0F),
          surface: Color(0xFF1E1E1E),
        ),
        appBarTheme: const AppBarTheme(
          backgroundColor: Colors.black,
          elevation: 0,
          centerTitle: true,
          titleTextStyle: TextStyle(
            color: Colors.amber,
            fontSize: 18,
            fontWeight: FontWeight.bold,
            letterSpacing: 1.2,
          ),
        ),
        sliderTheme: SliderThemeData(
          activeTrackColor: Colors.amber[700],
          inactiveTrackColor: Colors.white12,
          thumbColor: Colors.amber,
          overlayColor: Colors.amber.withOpacity(0.2),
          valueIndicatorColor: Colors.amber[800],
          valueIndicatorTextStyle: const TextStyle(
            color: Colors.black,
            fontWeight: FontWeight.bold,
          ),
        ),
        elevatedButtonTheme: ElevatedButtonThemeData(
          style: ElevatedButton.styleFrom(
            backgroundColor: const Color(0xFF1E1E1E),
            foregroundColor: Colors.white,
            padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(8),
              side: const BorderSide(color: Colors.white10),
            ),
          ),
        ),
      ),
      home: const MiletoControlPage(),
    );
  }
}
