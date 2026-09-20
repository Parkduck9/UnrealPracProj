# 설치 및 실행 가이드

웹 레지스트리를 띄우고, 데디케이티드 서버를 등록시키고, 클라이언트로 접속하기까지의 절차입니다.

## 준비물

| | 버전 | 확인 |
| --- | --- | --- |
| Unreal Engine | 5.8 | `PYJ_P38.uproject`의 `EngineAssociation` |
| Visual Studio | 2022 (C++ 게임 개발 워크로드) | `.vsconfig` 참고 |
| .NET SDK | 9.0 이상 | `dotnet --version` |

---

## 1단계 — 웹 레지스트리 실행

```bash
cd WebServer/ServerRegistry
dotnet run
```

기동 로그:

```
Server registry listening. Heartbeat every 10s, entries go offline after 30s and are purged after 300s.
warn: Using the default X-Server-Key. Change Registry:ServerApiKey before exposing this registry.
Now listening on: http://0.0.0.0:8080
```

브라우저에서 <http://localhost:8080> 을 열면 대시보드가 보입니다. 아직 서버가 없으므로
"등록된 서버가 없습니다"가 표시되면 정상입니다.

동작 확인:

```bash
curl http://localhost:8080/health
# {"status":"ok","servers":0,"sessions":0,"utc":"..."}
```

### 포트나 설정 바꾸기

`appsettings.json`을 고치거나 환경변수로 덮어씁니다.

```bash
# 포트 변경
dotnet run --urls http://0.0.0.0:9000

# 서버 키 변경 (소스에 키를 넣지 마세요)
Registry__ServerApiKey=my-secret-key dotnet run
```

PowerShell에서는 `$env:Registry__ServerApiKey = "my-secret-key"` 형태로 설정합니다.

언리얼 쪽 `Config/DefaultGame.ini`의 `ApiBaseUrl`, `ServerApiKey`도 같이 맞춰야 합니다.

---

## 2단계 — 언리얼 빌드

1. `PYJ_P38.uproject` 우클릭 → **Generate Visual Studio project files**
   (`Source/PYJ_P38Server.Target.cs`가 새로 추가되었으므로 이 단계가 필요합니다)
2. `PYJ_P38.sln`을 열고 **Development Editor | Win64** 빌드

### 빌드 확인

명령줄로도 됩니다.

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ^
  PYJ_P38Editor Win64 Development -project="<경로>\PYJ_P38\PYJ_P38.uproject" -waitmutex
```

`Build.cs`에 `HTTP`, `Json`, `Sockets`, `DeveloperSettings` 모듈이 추가되었으므로
첫 빌드는 평소보다 오래 걸립니다.

### 설정 확인

에디터에서 **Edit → Project Settings → Game → Server Registry** 에 항목이 보이면
`UServerRegistrySettings`가 제대로 올라온 것입니다.

---

## 3단계 — 데디케이티드 서버 실행

### 방법 A: 에디터에서 (가장 빠름)

툴바의 플레이 드롭다운 → **Net Mode: Play As Client**, **Number of Players: 2** 로 두면
에디터가 별도 서버 프로세스를 띄웁니다.

단, **PIE 서버는 `IsRunningDedicatedServer()`가 false라서 자동 등록되지 않습니다.**
에디터에서 등록 동작을 보려면 콘솔이나 블루프린트에서 `RegisterThisServer()`를 직접 호출하세요.
자동 등록까지 확인하려면 방법 B를 쓰는 편이 정확합니다.

### 방법 B: 에디터 실행 파일에 `-server` (에픽 런처 설치본에서 권장)

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" ^
  "<경로>\PYJ_P38\PYJ_P38.uproject" /Game/Make_P38 -server -log -port=7777 -ServerName="Seoul-01"
```

`-server`를 주면 `IsRunningDedicatedServer()`가 true가 되어 `PYJ_P38Server.exe`와 똑같이
자동 등록됩니다. 별도 빌드가 필요 없어 가장 빠르게 확인할 수 있습니다.

### 방법 C: 서버 타깃 빌드 후 실행

> **주의 — 에픽 런처로 설치한 엔진에서는 이 방법을 쓸 수 없습니다.**
> 빌드하면 `Server targets are not currently supported from this engine distribution` 오류가 납니다.
> GitHub 소스로 빌드한 엔진이 필요합니다. 런처 설치본을 쓰고 있다면 **방법 B**를 사용하세요.
> `Source/PYJ_P38Server.Target.cs`는 소스 빌드 엔진에서 그대로 쓸 수 있도록 포함되어 있습니다.

Visual Studio에서 **PYJ_P38Server | Development Server | Win64** 를 빌드한 뒤:

```bash
cd <프로젝트>/Binaries/Win64
PYJ_P38Server.exe /Game/Maps/YourMap -port=7777 -log
```

성공 로그:

```
LogServerRegistry: Server registry subsystem ready. Registry=http://127.0.0.1:8080 Role=DedicatedServer
LogServerRegistry: Registering server: {"serverName":"MY-PC","ip":"","port":7777,...}
LogServerRegistry: Registered as a3f2... Clients will be sent to 127.0.0.1:7777
LogServerRegistry: Heartbeat started at 10.0s.
```

대시보드를 새로고침하면 서버가 `ONLINE`으로 나타납니다.

### 서버 여러 대 띄우기

포트와 이름만 다르게 주면 됩니다.

```bash
PYJ_P38Server.exe /Game/Maps/YourMap -port=7777 -ServerName="Seoul-01" -log
PYJ_P38Server.exe /Game/Maps/YourMap -port=7778 -ServerName="Seoul-02" -log
```

클라이언트의 `JoinBestServer()`는 이 중 인원이 가장 적은 쪽을 고릅니다.

---

## 4단계 — 클라이언트로 접속

### 방법 A: UI 없이 자동 로그인 (동작 확인용)

```bash
PYJ_P38.exe -RegistryUser=pilot -RegistryPassword=1234 -log
```

`DefaultGame.ini`에서 `bAutoLoginOnClientStart=True`로 바꾸거나, 위처럼 계정만 넘겨도
`-RegistryUser=`가 있으면 켜집니다.

로그:

```
LogServerRegistry: Auto-login as 'pilot' (AutoJoin=true)
LogServerRegistry: Logged in as 'pilot'.
LogServerRegistry: Received 2 server(s) from the registry.
LogServerRegistry: Travelling to 127.0.0.1:7777 (Seoul-01).
```

### 방법 B: 블루프린트로 로그인 UI 만들기

로그인 위젯에서 `Get Game Instance Subsystem (Server Registry Subsystem)` 노드를 가져온 뒤:

1. **이벤트 바인딩** — `OnLoginCompleted`, `OnServerListReceived`, `OnJoinServer`에
   각각 커스텀 이벤트를 연결합니다. 모두 `bSuccess`와 `Message`를 주므로 실패 사유를
   그대로 화면에 띄울 수 있습니다.
2. **로그인 버튼** → `Login(Username, Password)`
3. `OnLoginCompleted`에서 성공이면 → `Request Server List`
4. `OnServerListReceived`에서 받은 `Servers` 배열로 목록 UI를 채웁니다
   (`FGameServerInfo`의 `ServerName`, `CurrentPlayers`, `MaxPlayers`, `Region` 사용)
5. 항목 클릭 → `Join Server (Server Info)` 또는 `Join Server By Id (ServerId)`

버튼 하나로 끝내려면 `Login And Join Best Server(Username, Password)` 하나만 호출하면 됩니다.

### 인원수를 정확히 표시하려면

기본적으로 하트비트는 `GameState->PlayerArray.Num()`를 읽어 자동으로 인원을 보고합니다.
별도 계산식을 쓰고 싶으면(관전자 제외 등) 서버에서 `Set Published Player Count(N)`을
호출하면 그 값이 우선합니다.

---

## 다른 PC에서 접속하기

1. **레지스트리를 외부에 노출** — `appsettings.json`의 `Urls`가 `http://0.0.0.0:8080`인지 확인.
   `localhost:8080`으로 두면 그 PC에서만 접속됩니다.
2. **방화벽 열기** — 레지스트리 8080/TCP, 게임 서버 7777/UDP.

   ```powershell
   New-NetFirewallRule -DisplayName "ServerRegistry" -Direction Inbound -LocalPort 8080 -Protocol TCP -Action Allow
   New-NetFirewallRule -DisplayName "PYJ_P38 Game" -Direction Inbound -LocalPort 7777 -Protocol UDP -Action Allow
   ```

3. **클라이언트에 레지스트리 주소 지정**

   ```bash
   PYJ_P38.exe -RegistryUrl=http://192.168.0.10:8080 -RegistryUser=pilot -RegistryPassword=1234
   ```

4. **서버도 같은 주소를 보게** 합니다.

   ```bash
   PYJ_P38Server.exe /Game/Maps/YourMap -port=7777 -RegistryUrl=http://192.168.0.10:8080
   ```

IP는 서버가 직접 정하지 않고 레지스트리가 요청 주소에서 읽으므로, 서버 쪽에
자기 IP를 따로 알려줄 필요가 없습니다.

---

## 패키징

**Platforms → Windows → Package Project** 로 클라이언트를 패키징합니다.
`Config/DefaultGame.ini` 설정이 함께 쿠킹되며, 커맨드라인 오버라이드가 항상 우선하므로
**한 번 패키징한 빌드로 개발/테스트/운영 레지스트리를 모두 쓸 수 있습니다.**

```bash
PYJ_P38.exe -RegistryUrl=https://registry.example.com
```

데디케이티드 서버는 **Build Target: PYJ_P38Server** 로 지정해 패키징하거나,
빌드된 `Binaries/Win64/PYJ_P38Server.exe`를 콘텐츠와 함께 배포합니다.

---

## 문제 해결

### 서버가 목록에 안 뜸

로그에서 `LogServerRegistry`를 확인하세요.

| 로그 | 원인 | 해결 |
| --- | --- | --- |
| `ApiBaseUrl resolved to 'http:', which is not a usable URL` | **INI에서 URL을 따옴표로 감싸지 않음.** 언리얼 설정 파서가 `//`를 주석으로 보고 잘라냅니다 | `ApiBaseUrl="http://127.0.0.1:8080"` 처럼 따옴표로 감싸세요 |
| `Could not reach the registry. Is the web server running?` | 레지스트리 미실행 또는 주소 오류 | `curl http://<주소>/health` 로 확인 |
| `Missing or invalid X-Server-Key (HTTP 401)` | 키 불일치 | `DefaultGame.ini`의 `ServerApiKey`와 `appsettings.json`의 `Registry:ServerApiKey` 비교 |
| `Role=Client` 로 찍힘 | 데디케이티드 서버 빌드가 아님 | `PYJ_P38Server.exe`(Server 타깃)로 실행 |
| 아무 로그도 없음 | `bAutoRegisterOnDedicatedServer=False` | `DefaultGame.ini` 확인 |

### 목록에는 뜨는데 접속이 안 됨

대시보드에 표시된 `ADDRESS`가 클라이언트에서 실제로 닿는 주소인지 확인하세요.

- `127.0.0.1`인데 다른 PC에서 접속 → 서버와 레지스트리가 같은 PC에 있어 루프백으로 기록된 경우.
  서버를 `-PublicIp=192.168.0.10` 으로 실행하세요.
- 사설 IP(`192.168.x.x`)인데 외부에서 접속 → 공인 IP를 `-PublicIp=`로 지정하고
  공유기에서 7777/UDP 포트포워딩을 설정하세요.
- 주소는 맞는데 안 됨 → 게임 포트(UDP)가 방화벽에 막혔을 가능성이 큽니다.
  레지스트리(TCP 8080)와 게임(UDP 7777)은 **별개 포트**입니다.

### 서버가 목록에서 계속 사라짐

하트비트가 레지스트리에 닿지 않는 상태입니다. 서버 로그에
`Heartbeat could not reach the registry` 가 반복되는지 확인하세요.
`OFFLINE`으로 바뀌는 기준은 기본 30초(하트비트 10초 × 3회)입니다.

### 클라이언트 로그인 실패

| 메시지 | 해결 |
| --- | --- |
| `Username must be 2-24 characters...` | 아이디를 영문/숫자/`_`/`.`/`-` 2~24자로 |
| `Password must be at least 4 characters.` | 비밀번호 4자 이상으로 |
| `Could not reach the registry...` | `-RegistryUrl` 주소와 방화벽 확인 |

### 로그를 더 자세히 보려면

```bash
PYJ_P38Server.exe ... -LogCmds="LogServerRegistry Verbose"
```

하트비트 성공까지 전부 찍힙니다.
