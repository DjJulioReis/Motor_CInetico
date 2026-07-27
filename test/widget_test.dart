import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import '../main.dart';

void main() {
  testWidgets('Mileto app smoke test', (WidgetTester tester) async {
    // Inicializa o app MiletoKineticApp
    await tester.pumpWidget(const MiletoKineticApp());

    // Verifica que o título está sendo exibido corretamente
    expect(find.text('Painel de Motores Cinéticos Mileto'), findsOneWidget);
  });
}
