# 아키텍처

서버 레지스트리 기능의 설계 의도와, 왜 그렇게 만들었는지에 대한 문서입니다.
"무엇을 호출하는가"는 [API.md](API.md), "어떻게 실행하는가"는 [SETUP.md](SETUP.md)에 있습니다.

## 풀려는 문제

언리얼 데디케이티드 서버에 접속하려면 클라이언트가 `IP:포트`를 알아야 합니다. 보통은
빌드에 박아 넣거나 플레이어에게 직접 알려주는데, 서버를 옮기거나 여러 대 띄우면 곧바로 무너집니다.

그래서 **주소를 아는 쪽이 직접 알리게** 했습니다. 서버가 자기 주소를 중앙 레지스트리에
올리고, 클라이언트는 거기서 받아갑니다. 양쪽 모두 상대의 주소를 미리 알 필요가 없고,
레지스트리 주소 하나만 알면 됩니다.

## 구성 요소

| 구성 요소 | 위치 | 역할 |
| --- | --- | --- |
| `UServerRegistrySubsystem` | `PYJ_P38/Source/PYJ_P38/Network/` | 서버 등록·하트비트, 클라이언트 로그인·목록·접속 |
| `UServerRegistrySettings` | 같은 폴더 | 프로젝트 설정 + 커맨드라인 오버라이드 |
| `FGameServerInfo` | `ServerRegistryTypes.h` | 서버 한 대를 나타내는 블루프린트 구조체 |
| `RegistryStore` | `WebServer/ServerRegistry/` | 인메모리 서버·세션 저장소, 만료 처리 |
| `Program.cs` | 같은 폴더 | REST 엔드포인트 정의 |
| `wwwroot/index.html` | 같은 폴더 | 실시간 대시보드 |

---

## 시퀀스

### 서버 시작 → 등록

```
PYJ_P38Server.exe              UServerRegistrySubsystem            웹 레지스트리
      │                                   │                             │
      │ 맵 로드 완료                        │                             │
      ├──── PostLoadMapWithWorld ────────▶│                             │
      │                                   │ IsRunningDedicatedServer()  │
      │                                   │ → true                      │
      │                                   │                             │
      │                                   │ POST /api/servers/register  │
      │                                   │ X-Server-Key, ip:"" port:7777
      │                                   ├────────────────────────────▶│
      │                                   │                             │ 요청 소켓에서
      │                                   │                             │ IP 확인 → 저장
      │                                   │◀──── 200 {serverId, ip} ────┤
      │                                   │                             │
      │                                   │ FTSTicker 10초 주기 시작      │
      │                                   │ POST /heartbeat (인원수)     │
      │                                   ├────────────────────────────▶│
      │                                   │            ⋮ 반복 ⋮          │
```

### 클라이언트 로그인 → 접속

```
로그인 위젯            UServerRegistrySubsystem              웹 레지스트리
   │                           │                                 │
   ├── LoginAndJoinBestServer ▶│                                 │
   │                           │ POST /api/auth/login            │
   │                           ├────────────────────────────────▶│
   │                           │◀───────── 200 {token} ──────────┤
   │◀── OnLoginCompleted ──────┤                                 │
   │                           │ GET /api/servers                │
   │                           │ Authorization: Bearer <token>   │
   │                           ├────────────────────────────────▶│
   │                           │◀──── 200 {servers:[...]} ───────┤
   │◀── OnServerListReceived ──┤                                 │
   │                           │ 인원 최소 서버 선택               │
   │◀── OnJoinServer ──────────┤                                 │
   │                           │ ClientTravel("10.0.0.9:7777")   │
   │                           ├───────── 게임 서버로 직접 접속 ────▶ (데디 서버)
```

접속 자체는 레지스트리를 거치지 않습니다. 레지스트리는 **주소를 알려주는 역할까지만** 하고,
게임 트래픽은 클라이언트와 데디 서버가 직접 주고받습니다. 레지스트리가 죽어도 이미 접속 중인
플레이어는 영향을 받지 않습니다.

---

## 설계 판단

### IP를 서버가 아니라 레지스트리가 정한다

언리얼 쪽은 `ip`를 빈 문자열로 보내고, 레지스트리가 `HttpContext.Connection.RemoteIpAddress`를
기록합니다. 서버가 `ISocketSubsystem::GetLocalHostAddr()`로 알아낼 수 있는 건 사설 IP
(`192.168.x.x`)뿐이고, 그 주소를 외부 클라이언트에 알려주면 접속이 안 되기 때문입니다.
요청이 실제로 도착한 주소는 이미 NAT를 통과한 주소라서 정확합니다.

서버가 알아낸 사설 IP는 `localIp` 필드로 같이 보내되 **진단용으로만 저장**하고 클라이언트에는
내보내지 않습니다. 같은 LAN에서 테스트할 때 로그만 봐도 어떤 장비인지 구분됩니다.

고정 IP나 도메인을 쓰는 환경에서는 `-PublicIp=`로 명시할 수 있습니다.

### 포트는 `-port=`를 최우선으로 읽는다

`ResolveGamePort()`는 `World->URL.Port`를 먼저 보고 `-port=`로 덮어씁니다. 실제로 리슨 소켓을
여는 값이 커맨드라인 인자이기 때문에, 둘이 다르면 커맨드라인이 맞습니다. 여기가 어긋나면
목록에는 뜨는데 접속만 실패하는, 원인을 찾기 어려운 버그가 됩니다.

### 죽음을 두 가지 경로로 처리한다

| 종료 방식 | 처리 |
| --- | --- |
| 정상 종료 | `Deinitialize()`에서 `unregister` 호출 + `HttpManager::Flush(Shutdown)`로 전송 보장 → 즉시 목록에서 사라짐 |
| 강제 종료·크래시·네트워크 단절 | 하트비트가 끊김 → 30초 후 `online:false` → 300초 후 삭제 |

정상 종료만 처리하면 크래시한 서버가 목록에 영원히 남고, 타임아웃만 처리하면 정상 종료
후에도 30초 동안 유령 서버로 남습니다. 둘 다 필요합니다.

`Flush`를 호출하는 이유는, 언리얼의 HTTP 요청이 비동기라서 `ProcessRequest()`만 하고 프로세스가
종료되면 요청이 나가지 않기 때문입니다.

### 하트비트 404는 에러가 아니라 재등록 신호

레지스트리는 상태를 메모리에만 들고 있어서 재시작하면 전부 잊습니다. 이때 서버들이 그대로
하트비트를 계속 보내면 아무도 목록에 없는 상태가 유지됩니다.

그래서 모르는 `serverId`에 대해 404를 돌려주고, `HandleHeartbeatResponse()`가 404를 받으면
`RegisteredServerId`를 비우고 즉시 재등록합니다. 레지스트리를 재시작해도 최대 한 번의
하트비트 주기(10초) 안에 목록이 저절로 복구됩니다.

같은 `ip:port` 재등록 시 기존 항목을 교체하는 것도 같은 맥락입니다. 서버 프로세스가 크래시 후
재시작하면 옛 항목이 아직 만료 전이라 남아 있는데, 그대로 두면 같은 서버가 두 줄로 보입니다.

### 서버 키와 클라이언트 토큰을 분리한다

`/api/servers/*`(쓰기)는 `X-Server-Key`, `/api/servers`(읽기)는 Bearer 토큰을 씁니다.
게임 클라이언트 바이너리 안에는 서버 키가 들어가지 않으므로, 클라이언트를 분석한 사람이
가짜 서버를 목록에 올려 플레이어를 유인할 수 없습니다.

### 목록을 받아서 클라이언트가 고른다

`GET /api/servers/best`가 있는데도 언리얼의 `JoinBestServer()`는 `GET /api/servers`로 받은
목록에서 로컬로 고릅니다. 서버 목록 UI와 "바로 접속" 버튼이 같은 데이터를 쓰게 되고,
요청도 한 번으로 끝납니다. `best` 엔드포인트는 목록이 필요 없는 런처 같은 다른 클라이언트용입니다.

### 서브시스템 하나로 양쪽을 처리한다

서버용과 클라이언트용을 나누지 않고 `UGameInstanceSubsystem` 하나에 넣었습니다.
`IsRunningDedicatedServer()`로 역할을 갈라 쓰고, 등록·조회가 같은 JSON 스키마를 공유하므로
파싱 코드가 한 곳에 모입니다. 클래스를 나누면 `FGameServerInfo` 파싱이 양쪽에 중복됩니다.

`Initialize()`가 아니라 `PostLoadMapWithWorld`에서 등록하는 이유는, 서브시스템 초기화 시점에는
월드가 없어서 맵 이름도 포트도 읽을 수 없기 때문입니다.

### 하트비트에 월드 타이머 대신 `FTSTicker`를 쓴다

`FTimerManager`는 월드에 묶여 있어 맵이 바뀌면 타이머가 사라집니다. 서버가 맵을 이동해도
등록은 유지되어야 하므로, 월드 수명과 무관한 코어 티커를 씁니다.

---

## 인증은 왜 이렇게 허술한가

이 프로젝트의 주제는 **서버 탐색**이지 계정 관리가 아닙니다. 그래서 로그인은
아이디 2~24자, 비밀번호 4자 이상이라는 **형식 검사만** 하고 토큰을 발급합니다.
비밀번호는 저장하지도 대조하지도 않습니다.

이렇게 둔 이유는, 어설프게 만든 인증이 제일 위험하기 때문입니다. 평문 비밀번호를 담은 사용자
테이블을 만들어 두면 "인증이 있다"는 착각을 주면서 실제로는 아무것도 지키지 못합니다.
차라리 명백하게 데모인 편이 낫다고 판단했습니다.

토큰 자체는 제대로 만듭니다. `RandomNumberGenerator.GetBytes(32)`로 생성한 256비트 난수이고,
12시간 후 만료되며, 만료된 세션은 백그라운드 작업이 정리합니다.

## 실서비스 전에 반드시 바꿔야 할 것

1. **진짜 인증** — 사용자 저장소 + 비밀번호 해싱(Argon2id/bcrypt) 또는 외부 신원 공급자.
   `POST /api/auth/login` 핸들러가 교체 지점이고, 그 아래 계층은 그대로 둬도 됩니다.
2. **HTTPS** — 지금은 평문 HTTP라 토큰과 서버 키가 네트워크에 그대로 노출됩니다.
   리버스 프록시(nginx, Caddy)를 앞에 두고 언리얼 쪽 `ApiBaseUrl`을 `https://`로 바꾸면 됩니다.
3. **서버 키 교체** — 기본값 `dev-server-key`를 그대로 쓰면 기동 시 경고 로그가 뜹니다.
   환경변수 `Registry__ServerApiKey`로 주입하고, 소스에는 넣지 마세요.
4. **영속 저장소** — 지금은 인메모리라 재시작하면 전부 잊습니다(서버는 자동 복구되지만
   세션은 끊깁니다). `RegistryStore`를 Redis나 DB 구현으로 교체하는 것을 전제로
   인터페이스가 한 클래스에 모여 있습니다.
5. **요청 제한(rate limit)** — 로그인 엔드포인트에 횟수 제한이 없습니다.
   ASP.NET Core의 `AddRateLimiter`로 붙일 수 있습니다.
6. **서버 신원 확인** — 지금은 공유 키 하나를 모든 서버가 같이 씁니다. 서버가 많아지면
   서버별 자격 증명이나 mTLS로 올라가야 합니다.

## 확장하기 좋은 지점

- **서버 선택 로직** — `RegistryStore.FindBestServer()`는 인원수만 봅니다.
  지연시간, 파티 인원 수용 가능 여부, 스킬 레이팅을 넣으려면 이 메서드만 고치면 됩니다.
- **매치메이킹 큐** — 레지스트리가 이미 모든 서버의 인원을 알고 있으므로, 대기열을 추가해
  `best` 대신 배정된 서버를 내려주는 형태로 확장할 수 있습니다.
- **서버 오토스케일** — `/health`의 `servers` 수와 각 서버 `currentPlayers`를 보고
  부족하면 새 데디 프로세스를 띄우는 오케스트레이터를 붙일 수 있습니다.
- **커스텀 메타데이터** — 게임 모드, 비밀번호 보호 여부 같은 필드는
  `RegisterRequest` / `ServerEntry` / `ServerListItem` / `FGameServerInfo` 네 곳에
  같은 이름으로 추가하면 끝입니다.

## 구성 파일의 함정 — URL은 반드시 따옴표로 감싼다

언리얼 설정 파서는 `//` 를 주석 시작으로 처리합니다. 그래서 이렇게 쓰면

```ini
ApiBaseUrl=http://127.0.0.1:8080
```

값이 조용히 `http:` 로 잘리고, 런타임에는 `libcurl error: 6 (Could not resolve hostname)` 라는
원인과 동떨어진 에러만 남습니다. 따옴표로 감싸야 합니다.

```ini
ApiBaseUrl="http://127.0.0.1:8080"
```

실제로 첫 통합 테스트에서 이 문제로 등록이 실패했습니다. 같은 일이 반복되지 않도록
`UServerRegistrySubsystem::Initialize()`가 URL이 `http://` 또는 `https://` 로 시작하는지 검사하고,
아니면 무엇을 고쳐야 하는지 로그로 알려줍니다.

## 검증 현황

### 웹 레지스트리 — `curl`로 전체 경로 확인

- 키 없는 등록 401, 토큰 없는 목록 조회 401, 잘못된 비밀번호 400
- `ip:""` 로 등록 시 요청 소켓 주소를 기록 (`127.0.0.1`)
- 하트비트로 인원수/맵 갱신, 모르는 id에 404
- `region` / `version` 필터, 인원 오름차순 정렬
- 같은 `ip:port` 재등록 시 중복 없이 교체
- 하트비트 중단 → `online:false` 전환 → 백그라운드 정리로 삭제
- 대시보드가 로그인부터 목록 표시까지 실제로 동작

### 언리얼 — 빌드 및 실제 데디케이티드 서버 구동 확인

UE 5.8로 `PYJ_P38Editor Win64 Development` 빌드 성공 (에러·경고 0).
이어서 실제 데디케이티드 서버를 띄워 전 구간을 확인했습니다.

```
LogServerRegistry: Server registry subsystem ready. Registry=http://127.0.0.1:8080 Role=DedicatedServer
LogServerRegistry: Registering server: {"serverName":"E2E-Test","ip":"","port":7777,"mapName":"Make_P38",...}
LogServerRegistry: Registered as 4c5b94b421064612ac4ef361bc9aaaf3. Clients will be sent to 127.0.0.1:7777
LogServerRegistry: Heartbeat started at 10.0s.
```

- 맵 로드 후 **자동 등록**, `IsRunningDedicatedServer()` 판별 정상
- 레지스트리가 요청 소켓에서 IP를 확정해 `127.0.0.1:7777` 로 회신
- 하트비트 10초 주기 동작 (`secondsSinceHeartbeat` 갱신 확인)
- 클라이언트 로그인 → `GET /api/servers` 에서 이 서버가 그대로 조회됨
- 서버를 강제 종료하자 `online:false` 로 바뀌고 기본 목록에서 제외됨

### 확인하지 못한 것

- **`PYJ_P38Server` 타깃 컴파일** — 에픽 런처로 설치한 엔진은 Server 타깃 빌드를 지원하지
  않습니다(`Server targets are not currently supported from this engine distribution`).
  타깃 파일 자체는 표준 형식이며, 소스 빌드 엔진에서 빌드해야 합니다.
  런처 설치본에서는 `UnrealEditor.exe <uproject> <맵> -server` 로 데디케이티드 서버를
  구동할 수 있고, 위 검증도 이 방식으로 했습니다. 자세한 내용은 [SETUP.md](SETUP.md#3단계--데디케이티드-서버-실행).
- **정상 종료 시 자동 해제** — `unregister` 엔드포인트 자체는 `curl`로 확인했지만,
  헤드리스 서버에 정상 종료 신호를 보낼 방법이 없어 언리얼 쪽 `Deinitialize()` 경로는
  실행해보지 못했습니다. 이 경우를 대비한 하트비트 만료 처리는 검증되었습니다.
- **클라이언트 `ClientTravel` 접속** — 패키징된 클라이언트가 필요해 확인하지 않았습니다.
  레지스트리가 내려주는 `ip`/`port` 값이 정확하다는 것까지는 위에서 확인했습니다.
