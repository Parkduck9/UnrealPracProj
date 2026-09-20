# UnrealPracProj — PYJ_P38

P-38 비행기를 조종하고 로켓을 발사하는 Unreal Engine 5.8 C++ 연습 프로젝트입니다.

여기에 **서버 레지스트리(Server Registry)** 기능이 추가되어 있습니다. 데디케이티드 서버가 스스로
자기 IP를 웹서버에 등록하고, 클라이언트는 로그인해서 그 목록을 받아 접속합니다. 접속 주소를
빌드에 하드코딩하거나 플레이어에게 직접 알려줄 필요가 없습니다.

```
  ┌──────────────────────┐   ① 등록 + 하트비트   ┌──────────────────────┐
  │  언리얼 데디 서버      │ ───────────────────▶ │   웹 레지스트리        │
  │  PYJ_P38Server.exe   │                      │   ServerRegistry     │
  └──────────────────────┘                      │   (ASP.NET Core)     │
             ▲                                  └──────────────────────┘
             │                                             ▲   │
             │ ③ ClientTravel("IP:포트")                    │   │
             │                            ② 로그인 → 서버 목록 │   │
  ┌──────────────────────┐ ────────────────────────────────┘   │
  │  언리얼 클라이언트      │ ◀───────────────────────────────────┘
  └──────────────────────┘
```

---

## 추가된 기능

### 1. 서버로 실행하면 웹서버에 자기 IP를 등록

`PYJ_P38Server.exe`(데디케이티드 서버)가 맵 로드를 마치면 `UServerRegistrySubsystem`이 자동으로
`POST /api/servers/register`를 호출합니다.

- **IP는 웹서버가 판단합니다.** 언리얼은 IP를 비워서 보내고, 레지스트리가 요청이 들어온 소켓의
  주소를 기록합니다. 공유기(NAT) 뒤에 있는 서버는 자기 공인 IP를 알 수 없기 때문입니다.
  고정 주소를 쓰고 싶으면 `-PublicIp=1.2.3.4`로 덮어쓸 수 있습니다.
- **포트**는 실제로 리슨 소켓을 여는 `-port=` 값을 그대로 씁니다.
- 등록 후 10초마다 하트비트를 보내 현재 인원수와 맵 이름을 갱신합니다.
- 서버가 조용해지면(하트비트 3회 누락) 목록에서 `OFFLINE`이 되고, 5분 뒤 완전히 삭제됩니다.
  그래서 서버가 강제 종료되어도 목록에 유령 서버가 남지 않습니다.
- 반대로 레지스트리가 재시작되어 서버를 잊어버리면 하트비트가 `404`를 받고, 서버는 스스로
  재등록합니다. 양쪽 어느 쪽이 죽어도 사람이 손댈 필요가 없습니다.

### 2. 클라이언트로 로그인하면 등록된 서버 IP를 받아 접속

`Login()` → `RequestServerList()` → `JoinServer()` 3단계이고, 전부 블루프린트에서 호출할 수 있습니다.
한 번에 처리하는 `LoginAndJoinBestServer()`도 있습니다.

- 로그인하면 Bearer 토큰을 받고, 이후 서버 목록 요청에 이 토큰이 필요합니다.
- 받은 목록에서 서버를 고르면 `IP:포트` 문자열을 `ClientTravel`에 넘겨 접속합니다.
- `JoinBestServer()`는 온라인이면서 자리가 남은 서버 중 인원이 가장 적은 곳을 고릅니다.
- UI를 만들기 전에 흐름만 확인하고 싶으면 `-RegistryUser=pilot -RegistryPassword=1234`로
  실행해서 위젯 없이 자동 로그인·접속시킬 수 있습니다.

---

## 빠른 시작

### 1) 웹 레지스트리 실행

```bash
cd WebServer/ServerRegistry
dotnet run
```

`http://localhost:8080` 을 열면 등록된 서버가 실시간으로 보이는 대시보드가 있습니다.
(.NET 9 SDK 필요)

### 2) 언리얼 프로젝트 빌드

`PYJ_P38.uproject`를 우클릭 → **Generate Visual Studio project files** 후 빌드합니다.
`Source/PYJ_P38Server.Target.cs`가 추가되어 있으므로 **PYJ_P38Server** 타깃이 생깁니다.

### 3) 데디케이티드 서버 실행

에픽 런처로 설치한 엔진은 Server 타깃을 빌드할 수 없으므로, 에디터 실행 파일에 `-server`를
붙이는 방식이 가장 간단합니다. (`IsRunningDedicatedServer()`가 동일하게 true가 됩니다)

```bash
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" \
  "<경로>\PYJ_P38\PYJ_P38.uproject" /Game/Make_P38 -server -log -port=7777
```

소스 빌드 엔진을 쓴다면 `PYJ_P38Server.exe /Game/Make_P38 -port=7777 -log` 도 동일합니다.

로그에 `LogServerRegistry: Registered as <id>. Clients will be sent to 127.0.0.1:7777` 이 뜨면
성공입니다. 대시보드에도 바로 나타납니다.

### 4) 클라이언트 실행

```bash
PYJ_P38.exe -RegistryUser=pilot -RegistryPassword=1234
```

로그인 → 서버 목록 수신 → 가장 한가한 서버로 접속까지 자동으로 진행됩니다.

자세한 절차(에디터 테스트, 패키징, 다른 PC에서 접속)는 **[Docs/SETUP.md](Docs/SETUP.md)** 를 보세요.

---

## 문서

| 문서 | 내용 |
| --- | --- |
| [Docs/SETUP.md](Docs/SETUP.md) | 설치·빌드·실행 절차, 에디터/패키지/원격 접속 테스트 방법, 문제 해결 |
| [Docs/API.md](Docs/API.md) | REST API 전체 명세 (요청/응답 예시, 상태 코드, curl 예제) |
| [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md) | 설계 의도, 시퀀스 다이어그램, 선택한 트레이드오프, 실서비스 전 보완할 점 |

---

## 소스 구성

```
PYJ_P38/
├── Source/
│   ├── PYJ_P38Server.Target.cs             데디케이티드 서버 빌드 타깃 (신규)
│   └── PYJ_P38/
│       ├── PYJ_P38.Build.cs                HTTP/Json/Sockets 모듈 의존성 추가
│       ├── MyPawn.*, MyRocket.*            기존 게임플레이 코드 (변경 없음)
│       └── Network/                        신규
│           ├── ServerRegistryTypes.h       FGameServerInfo, FRegistryLoginResult
│           ├── ServerRegistrySettings.*    프로젝트 설정 + 커맨드라인 오버라이드
│           └── ServerRegistrySubsystem.*   등록/하트비트/로그인/접속 로직
└── Config/DefaultGame.ini                  [/Script/PYJ_P38.ServerRegistrySettings] 추가

WebServer/ServerRegistry/                   신규 — ASP.NET Core 최소 API
├── Program.cs                              엔드포인트 정의
├── RegistryStore.cs                        인메모리 저장소 + 만료 정리 백그라운드 작업
├── Models.cs                               요청/응답 DTO, 설정 클래스
├── appsettings.json                        포트, API 키, 하트비트 주기
└── wwwroot/index.html                      실시간 대시보드
```

## 설정

`Config/DefaultGame.ini`의 `[/Script/PYJ_P38.ServerRegistrySettings]` 섹션에서 바꾸거나,
에디터에서 **Project Settings → Game → Server Registry** 로 편집합니다.
모든 값은 커맨드라인으로 덮어쓸 수 있어서, 한 번 패키징한 빌드를 여러 환경에 쓸 수 있습니다.

| 커맨드라인 | 용도 |
| --- | --- |
| `-RegistryUrl=http://host:8080` | 접속할 레지스트리 주소 |
| `-RegistryKey=...` | 서버 등록용 공유 키 |
| `-ServerName="Seoul #1"` | 목록에 표시될 서버 이름 |
| `-PublicIp=1.2.3.4` | 자동 감지 대신 쓸 공인 IP |
| `-RegistryUser=` / `-RegistryPassword=` | 클라이언트 자동 로그인 계정 |

## 검증 상태

- 웹 레지스트리: 전체 엔드포인트를 `curl`로 확인 (인증 실패, IP 자동 판별, 하트비트,
  오프라인 전환, 만료 삭제, 필터, 대시보드)
- 언리얼: UE 5.8 에디터 타깃 빌드 성공(에러·경고 0) + 실제 데디케이티드 서버를 띄워
  **자동 등록 → 하트비트 → 클라이언트 목록 조회**까지 동작 확인

확인하지 못한 부분(Server 타깃 컴파일, 정상 종료 시 자동 해제, 클라이언트 실제 접속)은
[Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md#확인하지-못한-것)에 그대로 적어두었습니다.

## 주의

**`DefaultGame.ini`에서 URL은 반드시 따옴표로 감싸세요.** 언리얼 설정 파서가 `//`를 주석으로
처리해서, 따옴표가 없으면 `http://127.0.0.1:8080` 이 `http:` 로 잘립니다.

레지스트리의 로그인은 **형식만 검사하는 데모**입니다. 아이디 2~24자, 비밀번호 4자 이상이면
누구나 토큰을 받습니다. 계정 시스템을 대체하는 물건이 아니므로, 실제 서비스에 쓰기 전에
[Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md#실서비스-전에-반드시-바꿔야-할-것)의 목록을 확인하세요.
