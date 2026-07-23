import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class MiletoControlPage extends StatefulWidget {
  const MiletoControlPage({Key? key}) : super(key: key);

  @override
  State<MiletoControlPage> createState() => _MiletoControlPageState();
}

class _MiletoControlPageState extends State<MiletoControlPage> {
  static const String serviceUuid = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
  static const String txUuid = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
  static const String rxUuid = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

  BluetoothDevice? targetDevice;
  BluetoothCharacteristic? txChar;
  BluetoothCharacteristic? rxChar;

  bool isScanning = false;
  bool isConnected = false;
  bool isAuthenticated = false;
  List<BluetoothDevice> scanResults = [];

  // Telemetria do Motor Cinético com Encoder
  bool isCalibrated = false;
  bool isHoming = false;
  int currentPosition = 0;
  int targetPosition = 0;
  int startLimit = 0;
  int endLimit = 10000;
  double currentPosMM = 0.0;
  double targetPosMM = 0.0;
  int motorEncoderSteps = 0;
  int stepsDeviation = 0;

  StreamSubscription? rxSubscription;

  @override
  void initState() {
    super.initState();
    initBluetooth();
  }

  void initBluetooth() {
    FlutterBluePlus.isScanning.listen((scanning) {
      if (mounted) {
        setState(() {
          isScanning = scanning;
        });
      }
    });
  }

  void startScan() async {
    setState(() {
      scanResults.clear();
    });
    await FlutterBluePlus.startScan(timeout: const Duration(seconds: 4));
    FlutterBluePlus.scanResults.listen((results) {
      if (mounted) {
        setState(() {
          scanResults = results.map((r) => r.device).toList();
        });
      }
    });
  }

  void connectToDevice(BluetoothDevice device) async {
    await device.connect();
    setState(() {
      targetDevice = device;
      isConnected = true;
    });

    List<BluetoothService> services = await device.discoverServices();
    for (var service in services) {
      if (service.uuid.toString().toLowerCase() == serviceUuid) {
        for (var char in service.characteristics) {
          if (char.uuid.toString().toLowerCase() == rxUuid) {
            rxChar = char;
          } else if (char.uuid.toString().toLowerCase() == txUuid) {
            txChar = char;
            await char.setNotifyValue(true);
            rxSubscription = char.onValueReceived.listen((value) {
              final text = utf8.decode(value);
              handleIncomingBleMessage(text);
            });
          }
        }
      }
    }
  }

  void handleIncomingBleMessage(String text) {
    if (!mounted) return;

    if (text.startsWith("AUTH_CHALLENGE:")) {
      final challengeStr = text.split(":")[1].trim();
      final challenge = int.tryParse(challengeStr);
      if (challenge != null) {
        final response = (challenge * 2) + 7;
        sendRawCommand("AUTH_RESPONSE:$response");
      }
    } else if (text.startsWith("MILETO_AUTH:VALID")) {
      setState(() {
        isAuthenticated = true;
      });
    } else if (text.startsWith("STATS:")) {
      final statsPayload = text.split(":")[1].trim();
      final parts = statsPayload.split(",");
      if (parts.length >= 10) {
        setState(() {
          isCalibrated = parts[0] == "1";
          isHoming = parts[1] == "1";
          currentPosition = int.tryParse(parts[2]) ?? 0;
          targetPosition = int.tryParse(parts[3]) ?? 0;
          startLimit = int.tryParse(parts[4]) ?? 0;
          endLimit = int.tryParse(parts[5]) ?? 10000;
          currentPosMM = double.tryParse(parts[6]) ?? 0.0;
          targetPosMM = double.tryParse(parts[7]) ?? 0.0;
          motorEncoderSteps = int.tryParse(parts[8]) ?? 0;
          stepsDeviation = int.tryParse(parts[9]) ?? 0;
        });
      }
    }
  }

  void sendRawCommand(String cmd) async {
    if (rxChar != null) {
      await rxChar!.write(utf8.encode("$cmd\n"), withoutResponse: false);
    }
  }

  void setTargetPosition(int target) => sendRawCommand("SET_POS:$target");
  void setPointA() => sendRawCommand("SET_POINT_A:0");
  void setPointB() => sendRawCommand("SET_POINT_B:0");
  void triggerHoming() => sendRawCommand("CALIBRAR:0");
  void stopStepper() => sendRawCommand("PARAR:0");
  void saveConfig() => sendRawCommand("GRAVAR:0");

  @override
  void dispose() {
    rxSubscription?.cancel();
    targetDevice?.disconnect();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Aplicativo Mileto Kinetic Closed-Loop'),
        backgroundColor: Colors.black,
      ),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            if (!isConnected) ...[
              ElevatedButton.icon(
                onPressed: isScanning ? null : startScan,
                icon: const Icon(Icons.search),
                label: Text(isScanning ? 'Escaneando...' : 'Buscar Dispositivo Mileto'),
                style: ElevatedButton.styleFrom(backgroundColor: Colors.amber[800]),
              ),
              const SizedBox(height: 10),
              ListView.builder(
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                itemCount: scanResults.length,
                itemBuilder: (context, index) {
                  final dev = scanResults[index];
                  return ListTile(
                    title: Text(dev.platformName.isNotEmpty ? dev.platformName : 'Dispositivo Mileto'),
                    subtitle: Text(dev.remoteId.toString()),
                    trailing: const Icon(Icons.bluetooth_connected, color: Colors.amber),
                    onTap: () => connectToDevice(dev),
                  );
                },
              )
            ] else ...[
              Card(
                color: Colors.grey[900],
                elevation: 6,
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    children: [
                      Text(
                        'Conexão Mileto Malha Fechada',
                        style: Theme.of(context).textTheme.titleLarge?.copyWith(color: Colors.amber[700]),
                      ),
                      const Divider(color: Colors.white24),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Autenticado:', style: TextStyle(color: Colors.white70)),
                          Icon(
                            isAuthenticated ? Icons.lock_open : Icons.lock,
                            color: isAuthenticated ? Colors.green : Colors.red,
                          )
                        ],
                      ),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Status Calibração:', style: TextStyle(color: Colors.white70)),
                          Text(
                            isCalibrated ? 'Calibrado' : 'Sem Calibração',
                            style: TextStyle(color: isCalibrated ? Colors.green : Colors.amber),
                          )
                        ],
                      ),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Ação Homing:', style: TextStyle(color: Colors.white70)),
                          Text(isHoming ? 'Zerando motor...' : 'Normal', style: const TextStyle(color: Colors.white))
                        ],
                      ),
                      const Divider(color: Colors.white10),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Posição Real Cabo:', style: TextStyle(color: Colors.white70)),
                          Text('${currentPosMM.toStringAsFixed(1)} mm', style: const TextStyle(color: Colors.green, fontWeight: FontWeight.bold))
                        ],
                      ),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Leitura Real Encoder:', style: TextStyle(color: Colors.white70)),
                          Text('$motorEncoderSteps passos', style: const TextStyle(color: Colors.white))
                        ],
                      ),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Desvio de Passos (Erro):', style: TextStyle(color: Colors.white70)),
                          Text(
                            '$stepsDeviation passos',
                            style: TextStyle(color: stepsDeviation > 10 ? Colors.red : Colors.green, fontWeight: FontWeight.bold),
                          )
                        ],
                      ),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Limites gravados (A/B):', style: TextStyle(color: Colors.white70)),
                          Text('$startLimit / $endLimit', style: const TextStyle(color: Colors.white))
                        ],
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 20),
              if (isAuthenticated) ...[
                const Text('Controle Manual do Motor Passo', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
                Slider(
                  value: targetPosition.clamp(startLimit, endLimit).toDouble(),
                  min: startLimit.toDouble(),
                  max: endLimit.toDouble(),
                  activeColor: Colors.amber[800],
                  inactiveColor: Colors.grey,
                  onChanged: (val) {
                    setState(() {
                      targetPosition = val.toInt();
                    });
                    setTargetPosition(targetPosition);
                  },
                ),
                const SizedBox(height: 20),
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                  children: [
                    ElevatedButton(
                      onPressed: setPointA,
                      style: ElevatedButton.styleFrom(backgroundColor: Colors.blueAccent),
                      child: const Text('Gravar Início (A)'),
                    ),
                    ElevatedButton(
                      onPressed: setPointB,
                      style: ElevatedButton.styleFrom(backgroundColor: Colors.indigoAccent),
                      child: const Text('Gravar Fim (B)'),
                    ),
                  ],
                ),
                const SizedBox(height: 10),
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                  children: [
                    ElevatedButton.icon(
                      onPressed: triggerHoming,
                      icon: const Icon(Icons.refresh),
                      label: const Text('Zerar Motor (Homing)'),
                      style: ElevatedButton.styleFrom(backgroundColor: Colors.green),
                    ),
                    ElevatedButton.icon(
                      onPressed: stopStepper,
                      icon: const Icon(Icons.dangerous),
                      label: const Text('PARAR'),
                      style: ElevatedButton.styleFrom(backgroundColor: Colors.red),
                    ),
                  ],
                ),
                const SizedBox(height: 15),
                ElevatedButton.icon(
                  onPressed: saveConfig,
                  icon: const Icon(Icons.save),
                  label: const Text('Salvar na Memória NVS'),
                  style: ElevatedButton.styleFrom(backgroundColor: Colors.amber[800]),
                ),
              ] else ...[
                const Padding(
                  padding: EdgeInsets.symmetric(vertical: 20.0),
                  child: Center(
                    child: CircularProgressIndicator(color: Colors.amber),
                  ),
                ),
              ]
            ],
          ],
        ),
      ),
    );
  }
}
