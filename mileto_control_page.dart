import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

class MiletoControlPage extends StatefulWidget {
  const MiletoControlPage({Key? key}) : super(key: key);

  @override
  State<MiletoControlPage> createState() => _MiletoControlPageState();
}

class _MiletoControlPageState extends State<MiletoControlPage> {
  static const String serviceUuid = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
  static const String charCtrlUuid = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
  static const String charStatusUuid = "c7e462d0-eb14-41d3-a9d0-0870932258aa";

  BluetoothDevice? targetDevice;
  BluetoothCharacteristic? ctrlChar;
  BluetoothCharacteristic? statusChar;

  bool isScanning = false;
  bool isConnected = false;
  List<BluetoothDevice> scanResults = [];

  // Live status from device
  bool isCalibrated = false;
  bool isHoming = false;
  int currentPosition = 0;
  int targetPosition = 0;

  StreamSubscription? statusSubscription;

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
          if (char.uuid.toString().toLowerCase() == charCtrlUuid) {
            ctrlChar = char;
          } else if (char.uuid.toString().toLowerCase() == charStatusUuid) {
            statusChar = char;
            await char.setNotifyValue(true);
            statusSubscription = char.onValueReceived.listen((value) {
              if (value.length >= 10 && mounted) {
                setState(() {
                  isCalibrated = value[0] == 1;
                  isHoming = value[1] == 1;
                  currentPosition = (value[2] << 24) | (value[3] << 16) | (value[4] << 8) | value[5];
                  targetPosition = (value[6] << 24) | (value[7] << 16) | (value[8] << 8) | value[9];
                });
              }
            });
          }
        }
      }
    }
  }

  void sendCommand(List<int> bytes) async {
    if (ctrlChar != null) {
      await ctrlChar!.write(bytes, withoutResponse: false);
    }
  }

  void setTargetPosition(int target) {
    List<int> cmd = [
      0x01,
      (target >> 24) & 0xFF,
      (target >> 16) & 0xFF,
      (target >> 8) & 0xFF,
      target & 0xFF,
    ];
    sendCommand(cmd);
  }

  void setPointA() => sendCommand([0x02]);
  void setPointB() => sendCommand([0x03]);
  void triggerHoming() => sendCommand([0x04]);
  void stopStepper() => sendCommand([0x05]);

  @override
  void dispose() {
    statusSubscription?.cancel();
    targetDevice?.disconnect();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Mileto DMX App Controller'),
        backgroundColor: Colors.blueAccent,
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
                label: Text(isScanning ? 'Procurando...' : 'Procurar Dispositivo Mileto'),
              ),
              const SizedBox(height: 10),
              ListView.builder(
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                itemCount: scanResults.length,
                itemBuilder: (context, index) {
                  final dev = scanResults[index];
                  return ListTile(
                    title: Text(dev.platformName.isNotEmpty ? dev.platformName : 'Dispositivo Desconhecido'),
                    subtitle: Text(dev.remoteId.toString()),
                    trailing: const Icon(Icons.bluetooth),
                    onTap: () => connectToDevice(dev),
                  );
                },
              )
            ] else ...[
              Card(
                elevation: 4,
                child: Padding(
                  padding: const EdgeInsets.all(16.0),
                  child: Column(
                    children: [
                      Text(
                        'Conectado ao Mileto DMX',
                        style: Theme.of(context).textTheme.titleLarge?.copyWith(color: Colors.green),
                      ),
                      const Divider(),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Calibrado:'),
                          Icon(
                            isCalibrated ? Icons.check_circle : Icons.error_outline,
                            color: isCalibrated ? Colors.green : Colors.red,
                          )
                        ],
                      ),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Home status:'),
                          Text(isHoming ? 'Buscando zero...' : 'Operação Normal')
                        ],
                      ),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('Posição Atual:'),
                          Text('$currentPosition passos')
                        ],
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 20),
              const Text('Controle Manual de Posição', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
              Slider(
                value: targetPosition.toDouble(),
                min: 0,
                max: 10000,
                divisions: 100,
                label: '$targetPosition',
                onChanged: (val) {
                  setState(() {
                    targetPosition = val.toInt();
                  });
                  setTargetPosition(targetPosition);
                },
              ),
              const SizedBox(height: 10),
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                children: [
                  ElevatedButton(
                    onPressed: setPointA,
                    style: ElevatedButton.styleFrom(backgroundColor: Colors.blue),
                    child: const Text('Gravar Ponto A'),
                  ),
                  ElevatedButton(
                    onPressed: setPointB,
                    style: ElevatedButton.styleFrom(backgroundColor: Colors.indigo),
                    child: const Text('Gravar Ponto B'),
                  ),
                ],
              ),
              const SizedBox(height: 10),
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                children: [
                  ElevatedButton.icon(
                    onPressed: triggerHoming,
                    icon: const Icon(Icons.settings_backup_restore),
                    label: const Text('Calibrar Zero'),
                    style: ElevatedButton.styleFrom(backgroundColor: Colors.amber[800]),
                  ),
                  ElevatedButton.icon(
                    onPressed: stopStepper,
                    icon: const Icon(Icons.stop),
                    label: const Text('PARADA DE EMERGÊNCIA'),
                    style: ElevatedButton.styleFrom(backgroundColor: Colors.red),
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}
