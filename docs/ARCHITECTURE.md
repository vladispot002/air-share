# LAN-Drop System Architecture

## 1. Загальна діаграма шарів

```
+-----------------------------------------------------------+
|                      Presentation Layer                   |
|  - GtkDesktopApp (GTK3 UI: Desktop macOS HIG & Mobile)    |
|  - Presenters: Discovery, Transfer, Storage, Settings     |
+-----------------------------------------------------------+
                             |
                             v
+-----------------------------------------------------------+
|                        Core Engine                        |
|  - MulticastBeacon (UDP Discovery)                        |
|  - NetworkSession (TCP Socket Transport)                  |
|  - SharedFolderManager (Local Storage & PIN Security)     |
|  - FileChunkHasher (SHA-256 Block Verification)           |
+-----------------------------------------------------------+
       |                                      |
       v                                      v
+-----------------------+     +-----------------------------+
|    Peer Subsystem     |     |       Task Subsystem        |
|  - NetworkPeer (base) |     |  - TransferTask (base)      |
|  - LocalPeer (P2P)    |     |  - P2PTransferTask          |
|  - CloudPeer (Relay)  |     |  - HttpWebTransferTask      |
|  - filterPeers<T>()   |     |  - ChunkBuffer<T>           |
+-----------------------+     +-----------------------------+
                             |
                             v
+-----------------------------------------------------------+
|                      Mobile Subsystem                     |
|  - NfcPairingSession (NDEF protocol parser & generator)   |
|  - AirDropProtocolBridge (Bonjour mDNS & HTTP API)        |
|  - LandropMobileBridge (C-ABI FFI for Flutter/iOS/Android)|
+-----------------------------------------------------------+
```

## 2. Патерн MVP (Model-View-Presenter)
- **IDiscoveryView** <---> **DiscoveryPresenter**
- **ITransferView** <---> **TransferPresenter**
- **IStorageView** <---> **StoragePresenter**
- **ISettingsView** <---> **SettingsPresenter**

Розподіл дозволяє запускати повні модульні тести презентерів без необхідності ініціалізації середовища X11/Wayland.

## 3. Поліморфізм у системі
1. **Динамічний поліморфізм**:
   - `NetworkPeer` -> `LocalPeer`, `CloudPeer`
   - `TransferTask` -> `P2PTransferTask`, `HttpWebTransferTask`
2. **Статичний поліморфізм (шаблони)**:
   - `filterPeers<T>()`: фільтрація контейнерів вузлів за типом на етапі компіляції.
   - `ChunkBuffer<T>`: типізований кільцевий буфер блоків даних.
