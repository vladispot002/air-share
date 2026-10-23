# LAN-Drop System Architecture

## 1. Verified Architecture & Layer Boundaries

```
+---------------------------------------------------------------------------------------+
|                                  PRESENTATION LAYER                                   |
|   +------------------------------------+             +----------------------------+   |
|   |         QtDesktopApp (Qt 6)        |             |    GtkDesktopApp (GTK3)    |   |
|   |   - Modern QSS (Dark / Light)      |             |   - macOS HIG Theme (CSS)  |   |
|   |   - 4 Modular Widgets              |             |   - Traffic Lights         |   |
|   |   - Live 300ms Transfer Tick Timer |             |   - Mobile 390x812 Viewport|   |
|   |   - Direct Peer Selection Dialog   |             +----------------------------+   |
|   +------------------------------------+                                              |
|        ^                 ^                  ^                 ^                       |
|        |                 |                  |                 |                       |
|  IDiscoveryView    ITransferView       IStorageView    ISettingsView                  |
|        ^                 ^                  ^                 ^                       |
|        |                 |                  |                 |                       |
|  DiscoveryPresenter TransferPresenter StoragePresenter SettingsPresenter             |
+---------------------------------------------------------------------------------------+
                                         |
                                         v
+---------------------------------------------------------------------------------------+
|                                     CORE ENGINE                                       |
|   +----------------------+   +-------------------+   +----------------------------+   |
|   |   MulticastBeacon    |   |  NetworkSession   |   |      FileChunkHasher       |   |
|   | (UDP beacon parser & |   | (POSIX TCP Socket |   |   (Pure SHA-256 hash calc  |   |
|   |  serializer logic)   |   |  sendAll/recvEx)  |   |    & chunk verification)   |   |
|   +----------------------+   +-------------------+   +----------------------------+   |
|              +-----------------------------------+   +--------------------+           |
|              |        SharedFolderManager        |   |   ChunkBuffer<T>   |           |
|              |  (Symlink & Path Traversal Guards,|   | (Thread-safe FIFO) |           |
|              |   10GB Bound, Verified PIN-Lock)  |   +--------------------+           |
|              +-----------------------------------+                                    |
|              +-----------------------------------+   +--------------------+           |
|              |            TlsContext             |   |     HttpClient     |           |
|              | (OpenSSL 3 CA & Hostname Checking |   | (HTTPS Sync, Quota |           |
|              |  or Pinning, Insecure Opt-In)     |   |  & Truncation Guard|           |
|              +-----------------------------------+   +--------------------+           |
+---------------------------------------------------------------------------------------+
             |                                           |
             v                                           v
+-----------------------------+             +-------------------------------+
|       PEER SUBSYSTEM        |             |        TASK SUBSYSTEM         |
|  NetworkPeer (Abstract Base)|             |  TransferTask (Abstract Base) |
|   ├── LocalPeer (Subnet/P2P)|             |   ├── P2PTransferTask (Memory)|
|   └── CloudPeer (Relay/Sync)|             |   └── HttpWebTransferTask (QR)|
|  filterPeers<T>() (RTTI cast)|            |  State: Idle->Transfer->Done  |
+-----------------------------+             +-------------------------------+
                                         |
                                         v
+---------------------------------------------------------------------------------------+
|                               MOBILE INTEROP SUBSYSTEM                                |
|  +--------------------+  +--------------------------+  +---------------------------+  |
|  | NfcPairingSession  |  |  AirDropProtocolBridge   |  |    LandropMobileBridge    |  |
|  | - Semicolon-delim  |  | - Formats mock HTTP      |  | - C-ABI FFI Export        |  |
|  |   NDEF payload     |  |   responses (_airdrop)   |  | - Direct interop testing |  |
|  +--------------------+  +--------------------------+  +---------------------------+  |
+---------------------------------------------------------------------------------------+
```

## 2. Decoupled Presentation Layer (MVP)
The user interface strictly adheres to Model-View-Presenter (MVP):
- **`IDiscoveryView`** <---> **`DiscoveryPresenter`**
- **`ITransferView`** <---> **`TransferPresenter`**
- **`IStorageView`** <---> **`StoragePresenter`**
- **`ISettingsView`** <---> **`SettingsPresenter`**

This decoupling allows 100% headless testing via `MockView` (used in `tests/unit_tests.cpp`) as well as comprehensive Qt widget testing via `tests/test_qt_gui.cpp` without requiring an interactive desktop session.

## 3. Polymorphism in the System
1. **Dynamic Polymorphism**:
   - `NetworkPeer` -> `LocalPeer`, `CloudPeer`
   - `TransferTask` -> `P2PTransferTask`, `HttpWebTransferTask`
2. **Static Polymorphism & Templates**:
   - `ChunkBuffer<T>`: Thread-safe generic circular/queue buffer.
   - `filterPeers<T>()`: Template helper performing `std::dynamic_pointer_cast<T>` filtering across heterogeneous peer collections.

## 4. Build Systems & Targets
- **CMake** (Primary & Recommended):
  - Targets:
    - `landrop_core` (STATIC library with core business and networking logic).
    - `lan_drop` (Qt 6 executable GUI).
    - `lan_drop_gtk` (GTK+ 3.0 executable GUI).
    - `lan_drop_tests` (Comprehensive unit test suite, 319 assertions).
    - `lan_drop_gui_tests` (Qt 6 GUI test suite with QtTest, 15 test items).
- **Makefile**:
  - Targets: `all` (`bin/lan_drop` [GTK3]), `test` (`bin/lan_drop_tests`), `mobile-test`, `gui`, `mobile-gui`, `clean`.

## 5. Security & Boundary Architecture
1. **Core TransferEngine & Socket Streaming**:
   - Single unified `TransferEngine` shared between CLI commands (`send`/`receive`) and Qt 6 GUI.
   - Transferred files are received into temporary `*.part` files with chunked streaming (64 KB default up to 4 MB).
   - Speed limit throttling natively integrated into the socket streaming loop.
   - Upon completion, the full-file SHA-256 checksum is computed and validated against the sender header.
   - On match, `std::filesystem::rename` performs an atomic move to the target file.
   - On socket disconnect, premature abort, or checksum mismatch, the temporary `*.part` file is immediately deleted.
   - Responsive cancellation tokens abort transfer in-flight within <= 50ms without leaving orphaned sockets or files.
2. **Hardened Storage & PIN Security**:
   - Access PINs are hashed using **OpenSSL PBKDF2-HMAC-SHA256** with a 16-byte cryptographically secure random salt and 100,000 iterations (`$pbkdf2-sha256$i=100000$<salt_hex>$<key_hex>`).
   - Constant-time comparison using `CRYPTO_memcmp` protects against side-channel timing attacks.
   - Progressive brute-force delay penalty on consecutive failed attempts.
   - Automatic migration upgrades legacy unsalted SHA-256 PIN hashes on next successful verification.
   - Inbound file paths are sanitized via `isSafeFileName` and canonical traversal checks (`std::filesystem::relative`).
   - Symlink targets are strictly checked to ensure they do not escape the root shared directory.
   - Configurable file size boundary (default 10 GB limit) prevents storage exhaustion.
3. **Transport Layer Security (TLS)**:
   - Client connections verify server certificates and hostnames by default using the system CA store (`X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS`).
   - Self-signed certificates require providing the expected SHA-256 fingerprint (`--tls <fingerprint>`).
   - Insecure TLS mode is strictly opt-in (`allowInsecureTls`).

## 6. Implementation Status Matrix

| Функціональний модуль | Статус реалізації | Деталі та примітки |
|---|---|---|
| **Єдиний рушій передачі (`TransferEngine`)** | **Реалізовано та перевірено** | Спільний для CLI та Qt GUI, потоковий сокетний TCP-протокол, чанки 64 КБ–4 МБ, атомарне перейменування `.part`, SHA-256. |
| **Qt 6 Десктопний GUI** | **Реалізовано та перевірено** | 4 вкладки, темна/світла теми, вибір піра або direct IP:port, захист PIN, скасування передачі, 15/15 GUI тестів пройдено. |
| **GTK3 Десктопний & Мобільний GUI** | **Реалізовано та перевірено** | Збережено повну функціональність macOS HIG та адаптивного мобільного вікна 390x812. |
| **MVP Архітектура Presenter/View** | **Реалізовано та перевірено** | Повна ізоляція бізнес-логіки від UI; спільні Presenter для Qt та GTK. |
| **Мережева передача TCP (CLI `send`/`receive`)** | **Реалізовано та перевірено** | Переведено на `TransferEngine`; перевірено передачу 1 МБ та 10 МБ між процесами з побітовим збігом SHA-256. |
| **Безпека TLS/HTTPS (OpenSSL 3)** | **Реалізовано та перевірено** | За замовчуванням перевірка CA та імені хоста; відхилення truncated відповідей; опціональний `allowInsecureTls`. |
| **Криптостійкий PIN (PBKDF2-HMAC-SHA256)** | **Реалізовано та перевірено** | 16-байтна сіль, 100,000 ітерацій, `CRYPTO_memcmp`, прогресивна затримка проти перебору, авто-міграція застарілих хешів. |
| **Безпека сховища (Shared Storage)** | **Реалізовано та перевірено** | Блокування виходу за межі каталогу (Path Traversal), валідація цільових шляхів симлінків, ліміт 10 ГБ. |
| **Стійке збереження налаштувань** | **Реалізовано та перевірено** | Збереження в `AppConfigLocation`/XDG, парсинг застарілих ключів, збереження цілісності при пошкодженому JSON, синхронізація Speed Limit. |
| **Шаблонний буфер чанків `ChunkBuffer<T>`** | **Реалізовано та перевірено** | Потокобезпечний буфер FIFO, покритий тестами. |
| **Хешування SHA-256 (`FileChunkHasher`)** | **Реалізовано та перевірено** | Повна перевірка цілісності файлів і блоків, покритий тестами. |
| **C-ABI / FFI Bridge (`LandropMobileBridge`)** | **Реалізовано та перевірено** | Експорт функцій для зв'язку з Flutter/Kotlin/Swift, перевірено виклики. |
| **Обмеження швидкості передачі (Speed Limit)** | **Реалізовано та перевірено** | Логіка throttling реалізована в сокетному циклі `TransferEngine` та `TransferPresenter`, перевірена тестами. |
| **Справжнє переривання/відновлення (Pause/Resume)** | **Уточнено специфікацію** | У прямому сокетному стрімінгу без протокольного byte-offset resume кнопки Pause/Resume чесно вимкнені з підказками; працює повноцінне скасування (Cancel) з видаленням `.part`. |
| **AirDrop сумісність (`AirDropProtocolBridge`)** | **Частково реалізовано** | Формування протокольних структур Bonjour/HTTP `/Discover` та `/Ask` у пам'яті; без зв'язку з демоном Avahi та апаратним AWDL/BLE. |
| **NFC Fast Pairing (`NfcPairingSession`)** | **Частково реалізовано** | Формування та парсинг текстового NDEF payload `LANDROP:NFC:v1`; без взаємодії з апаратним NFC контролером Linux/Android. |
| **Хмарне реле / Квоти (`CloudPeer`, `HttpClient`)** | **Частково реалізовано** | HTTP клієнт виконує реальні TLS запити та валідацію REST JSON; робота зі справжнім сервером реле залежить від доступності віддаленого бекенду. |
| **Автоматичне мультикаст-виявлення в LAN** | **Не вдалося перевірити в цьому середовищі** | Логіка `MulticastBeacon` реалізована та протестована локально; повноцінне виявлення потребує двох фізичних/віртуальних пристроїв у спільній L2/L3 мережі без блокування IGMP/мультикасту. |

## 7. Environment Requirements for Physical Verification
For complete end-to-end testing of LAN-Drop across physical devices:
1. **Network Topology**: Two physical computers or bridged virtual machines on the same L2/L3 subnet.
2. **IGMP / Multicast**: Network switches/routers must forward UDP multicast packets on port 8848 (`239.255.88.48`). Wireless isolation must be disabled.
3. **Firewall Rules**: Open inbound TCP port 8848 (and UDP 8848) on both test machines.
4. **Certificates**: Custom CA root certificates installed in system trust store for TLS validation, or use fingerprint pinning.
