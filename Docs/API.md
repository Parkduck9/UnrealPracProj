# API 명세

서버 레지스트리 REST API 전체 명세입니다. 기본 주소는 `http://localhost:8080` 이며
`WebServer/ServerRegistry/appsettings.json`의 `Urls`로 바꿀 수 있습니다.

모든 요청과 응답은 `application/json` (UTF-8)입니다.

## 인증 방식 두 가지

| 대상 | 헤더 | 발급 방법 |
| --- | --- | --- |
| 데디케이티드 서버 | `X-Server-Key: <공유키>` | `appsettings.json`의 `Registry:ServerApiKey`에 미리 설정 |
| 클라이언트 | `Authorization: Bearer <토큰>` | `POST /api/auth/login` 응답의 `token` |

서버용 키와 클라이언트용 토큰을 나눈 이유는, 게임 클라이언트를 뜯어본 사람이 가짜 서버를
목록에 올리지 못하게 하기 위해서입니다. 클라이언트 토큰으로는 목록 조회만 됩니다.

실패 응답은 모두 같은 모양입니다.

```json
{ "message": "사람이 읽을 수 있는 실패 사유" }
```

언리얼 쪽 `UServerRegistrySubsystem::ExtractMessage()`가 이 `message`를 꺼내
`OnLoginCompleted` 같은 델리게이트로 그대로 전달하므로, UI에 바로 띄울 수 있습니다.

---

# 클라이언트 API

## POST /api/auth/login

로그인하고 Bearer 토큰을 받습니다. 인증 헤더가 필요 없는 유일한 엔드포인트입니다.

**요청**

```json
{ "username": "pilot", "password": "1234" }
```

**성공 200**

```json
{
  "token": "94d07c6c0b9b767dc01068c51b6e225c6fa9362598a437530348b21918161a8c",
  "username": "pilot",
  "expiresAtUtc": "2026-09-20T23:48:45.1107035Z"
}
```

**실패 400** — 아이디는 2~24자(영문·숫자·`_`·`.`·`-`), 비밀번호는 4자 이상이어야 합니다.

```json
{ "message": "Password must be at least 4 characters." }
```

> 이 로그인은 **형식만 검사하는 데모**입니다. 위 조건만 맞으면 어떤 아이디/비밀번호든 통과합니다.
> 비밀번호를 저장하지도, 대조하지도 않습니다. 이유와 대안은
> [ARCHITECTURE.md](ARCHITECTURE.md#인증은-왜-이렇게-허술한가)에 적어두었습니다.

**언리얼 호출 지점** — `UServerRegistrySubsystem::Login()`, `LoginAndJoinBestServer()`

---

## POST /api/auth/logout

토큰을 서버 쪽에서 폐기합니다. 헤더: `Authorization: Bearer <토큰>`.
토큰이 없거나 이미 만료됐어도 **200**입니다(멱등).

```json
{ "message": "Logged out." }
```

---

## GET /api/servers

등록된 서버 목록을 받습니다. 헤더: `Authorization: Bearer <토큰>`.

**쿼리 파라미터** (모두 선택)

| 이름 | 기본값 | 설명 |
| --- | --- | --- |
| `region` | 없음 | `KR` 처럼 지역으로 필터 (대소문자 무시) |
| `version` | 없음 | 빌드 버전으로 필터. 버전이 다른 서버에 접속시켜 튕기는 것을 막을 때 씁니다 |
| `includeOffline` | `false` | `true`면 하트비트가 끊긴 서버도 포함 (대시보드용) |

**성공 200** — 인원이 적은 순으로 정렬되어 옵니다.

```json
{
  "servers": [
    {
      "serverId": "c201689b71094cb68f4bc1494996c134",
      "serverName": "Seoul-01",
      "ip": "127.0.0.1",
      "port": 7777,
      "mapName": "OpenWorld",
      "currentPlayers": 7,
      "maxPlayers": 32,
      "region": "KR",
      "version": "1.0.0",
      "online": true,
      "secondsSinceHeartbeat": 6.2
    }
  ],
  "count": 1
}
```

각 필드는 언리얼의 `FGameServerInfo` 구조체와 1:1로 대응합니다.
`ip`와 `port`를 합친 `"127.0.0.1:7777"` 이 그대로 `ClientTravel`에 들어갑니다.

**실패 401**

```json
{ "message": "Log in first: send Authorization: Bearer <token>." }
```

**언리얼 호출 지점** — `UServerRegistrySubsystem::RequestServerList()`

---

## GET /api/servers/best

온라인이면서 자리가 남은 서버 중 **인원이 가장 적은 한 대**만 돌려줍니다.
서버 선택 UI 없이 "바로 접속" 버튼만 두고 싶을 때 씁니다.
헤더와 쿼리 파라미터(`region`, `version`)는 `GET /api/servers`와 같습니다.

**성공 200** — 목록과 같은 모양이고 항상 1개입니다.

**실패 404**

```json
{ "message": "No online server with free slots." }
```

> 언리얼 클라이언트는 이 엔드포인트 대신 `GET /api/servers`를 한 번 받아 로컬에서 고릅니다
> (`JoinBestServer()`). 서버 목록 UI와 자동 접속이 같은 데이터를 쓰게 되고, 요청도 한 번으로
> 끝나기 때문입니다. 이 엔드포인트는 목록이 필요 없는 다른 클라이언트(런처 등)를 위해 남겨둡니다.

---

# 데디케이티드 서버 API

세 엔드포인트 모두 `X-Server-Key` 헤더가 필요하고, 없거나 틀리면 **401**입니다.

```json
{ "message": "Missing or invalid X-Server-Key." }
```

## POST /api/servers/register

서버를 목록에 올립니다.

**요청**

```json
{
  "serverName": "Seoul-01",
  "ip": "",
  "port": 7777,
  "mapName": "OpenWorld",
  "maxPlayers": 32,
  "region": "KR",
  "version": "1.0.0",
  "localIp": "192.168.0.12"
}
```

| 필드 | 필수 | 설명 |
| --- | --- | --- |
| `serverName` | 아니오 | 비우면 `Unnamed Server`. 64자로 잘립니다 |
| `ip` | **아니오 (비우는 쪽을 권장)** | 비우거나 `"auto"`면 레지스트리가 **요청이 들어온 주소**를 기록합니다. NAT 뒤 서버는 자기 공인 IP를 모르므로 이쪽이 정확합니다 |
| `port` | **예** | 1~65535. 벗어나면 400 |
| `maxPlayers` | 아니오 | 0~1000으로 제한. 0이면 "정원 제한 없음"으로 취급 |
| `localIp` | 아니오 | 서버가 보고한 사설 IP. 진단용으로만 저장하고 클라이언트에는 내보내지 않습니다 |

**성공 200**

```json
{
  "serverId": "c201689b71094cb68f4bc1494996c134",
  "ip": "127.0.0.1",
  "port": 7777,
  "heartbeatIntervalSeconds": 10,
  "message": "Registered Seoul-01 at 127.0.0.1:7777."
}
```

`serverId`는 이후 하트비트/해제에 쓰는 키입니다. 언리얼은 이 값을 `RegisteredServerId`에
보관하고, 응답의 `ip`를 로그에 남겨 "클라이언트가 실제로 접속하게 될 주소"를 확인할 수 있게 합니다.

**같은 `ip:port`로 다시 등록하면** 기존 항목을 지우고 새로 만듭니다. 서버 프로세스가 비정상
종료 후 재시작했을 때 같은 서버가 두 줄로 보이지 않게 하기 위해서입니다.

**실패 400**

```json
{ "message": "Port must be between 1 and 65535." }
```

**언리얼 호출 지점** — `UServerRegistrySubsystem::RegisterThisServer()`

---

## POST /api/servers/heartbeat

등록을 살아있게 유지하고, 인원수와 맵 이름을 갱신합니다. 기본 10초 주기입니다.

**요청**

```json
{ "serverId": "c201689b71094cb68f4bc1494996c134", "currentPlayers": 7, "mapName": "Arena" }
```

`mapName`이 비어 있으면 기존 값을 유지합니다. 맵이 바뀌지 않는 하트비트는 이름을 굳이
다시 보내지 않아도 됩니다.

**성공 200**

```json
{ "message": "ok" }
```

**실패 404** — 레지스트리가 그 `serverId`를 모릅니다.

```json
{ "message": "Unknown serverId. Register again." }
```

404는 에러가 아니라 **재등록 신호**입니다. 레지스트리가 재시작됐거나 항목이 만료된 상황이며,
언리얼의 `HandleHeartbeatResponse()`가 이걸 받으면 `serverId`를 버리고 곧바로 재등록합니다.
운영자가 개입하지 않아도 목록이 복구됩니다.

---

## POST /api/servers/unregister

목록에서 내립니다. 서버가 정상 종료될 때 호출합니다.

**요청**

```json
{ "serverId": "c201689b71094cb68f4bc1494996c134" }
```

**성공 200** — 모르는 `serverId`여도 200입니다(멱등). 종료 경로에서 두 번 호출되어도 안전합니다.

```json
{ "message": "Unregistered." }
```

> 이 호출은 **최선 노력**입니다. 서버가 강제 종료되면 이 요청은 나가지 않습니다.
> 그래서 레지스트리는 하트비트 만료(기본 30초 후 `OFFLINE`, 300초 후 삭제)를 별도로 돌립니다.
> 정상 종료는 즉시 반영, 비정상 종료는 타임아웃으로 정리 — 두 경로 모두 막혀 있습니다.

---

# 운영

## GET /health

인증이 필요 없습니다. 모니터링이나 컨테이너 헬스체크용입니다.

```json
{
  "status": "ok",
  "servers": 2,
  "sessions": 3,
  "utc": "2026-09-20T11:48:34.4990938Z"
}
```

## GET /

실시간 대시보드(`wwwroot/index.html`)입니다. 2초마다 갱신되며
등록된 서버의 상태·주소·인원·마지막 하트비트 경과 시간을 보여줍니다.

이 대시보드는 특별한 관리자 API를 쓰지 않습니다. 게임과 똑같이 `POST /api/auth/login`으로
토큰을 받고 `GET /api/servers?includeOffline=true`를 호출합니다. 즉 대시보드가 보이면
클라이언트 경로도 살아있다는 뜻이라, 그 자체로 점검 도구가 됩니다.

---

# 전체 흐름 curl 예제

```bash
BASE=http://localhost:8080

# --- 서버 역할 ---
SERVER_ID=$(curl -s -X POST $BASE/api/servers/register \
  -H "Content-Type: application/json" -H "X-Server-Key: dev-server-key" \
  -d '{"serverName":"Seoul-01","ip":"","port":7777,"mapName":"OpenWorld","maxPlayers":32,"region":"KR"}' \
  | grep -o '"serverId":"[^"]*' | cut -d'"' -f4)

curl -s -X POST $BASE/api/servers/heartbeat \
  -H "Content-Type: application/json" -H "X-Server-Key: dev-server-key" \
  -d "{\"serverId\":\"$SERVER_ID\",\"currentPlayers\":7}"

# --- 클라이언트 역할 ---
TOKEN=$(curl -s -X POST $BASE/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"pilot","password":"1234"}' \
  | grep -o '"token":"[^"]*' | cut -d'"' -f4)

curl -s $BASE/api/servers -H "Authorization: Bearer $TOKEN"

# --- 서버 종료 ---
curl -s -X POST $BASE/api/servers/unregister \
  -H "Content-Type: application/json" -H "X-Server-Key: dev-server-key" \
  -d "{\"serverId\":\"$SERVER_ID\"}"
```

## 상태 코드 정리

| 코드 | 언제 | 받는 쪽이 할 일 |
| --- | --- | --- |
| 200 | 성공 | — |
| 400 | 요청 값이 잘못됨 (포트 범위, 아이디/비밀번호 형식) | 입력을 고쳐 재시도 |
| 401 | `X-Server-Key` 또는 Bearer 토큰이 없거나 틀림 | 서버는 키 확인, 클라이언트는 재로그인 |
| 404 | 하트비트의 `serverId`를 모름 / 접속 가능한 서버 없음 | 서버는 **재등록**, 클라이언트는 사용자에게 안내 |
