# ServerRegistry — 웹 레지스트리

언리얼 데디케이티드 서버가 자기 주소를 등록하고, 클라이언트가 그 목록을 받아가는
ASP.NET Core 최소 API 서버입니다.

```bash
cd ServerRegistry
dotnet run
```

- 대시보드: <http://localhost:8080>
- 헬스체크: <http://localhost:8080/health>

.NET 9 SDK가 필요합니다. 외부 패키지 의존성은 없습니다.

## 파일

| 파일 | 역할 |
| --- | --- |
| `Program.cs` | 엔드포인트 정의, 인증 검사, 클라이언트 IP 판별 |
| `RegistryStore.cs` | 인메모리 서버·세션 저장소, 만료 정리 백그라운드 작업 |
| `Models.cs` | 요청/응답 DTO, `RegistryOptions` 설정 클래스 |
| `appsettings.json` | 포트, 서버 키, 하트비트·만료 주기 |
| `wwwroot/index.html` | 실시간 대시보드 (의존성 없는 단일 HTML) |

## 설정

`appsettings.json`의 `Registry` 섹션, 또는 `Registry__키이름` 환경변수로 덮어씁니다.

| 키 | 기본값 | 설명 |
| --- | --- | --- |
| `ServerApiKey` | `dev-server-key` | 서버 등록용 공유 키. **배포 전 반드시 변경** |
| `HeartbeatIntervalSeconds` | `10` | 서버에 알려주는 하트비트 주기 |
| `OfflineAfterMissedHeartbeats` | `3` | 몇 번 놓치면 `OFFLINE` 처리할지 (기본 30초) |
| `PurgeAfterSeconds` | `300` | 이 시간이 지나면 항목을 완전히 삭제 |
| `SessionLifetimeHours` | `12` | 로그인 토큰 유효 시간 |
| `MinimumPasswordLength` | `4` | 데모 로그인의 최소 비밀번호 길이 |

```bash
Registry__ServerApiKey=my-secret dotnet run --urls http://0.0.0.0:9000
```

## 상태 저장

모든 상태는 프로세스 메모리에만 있습니다. 재시작하면 등록 정보와 세션이 사라지지만,
게임 서버는 다음 하트비트에서 `404`를 받고 스스로 재등록하므로 10초 안에 목록이 복구됩니다.

영속화가 필요하면 `RegistryStore`를 DB 구현으로 교체하면 됩니다. 저장소 접근이 이 클래스
하나에 모여 있습니다.

## API

전체 명세는 [../Docs/API.md](../Docs/API.md)를 보세요.

| 메서드 | 경로 | 인증 |
| --- | --- | --- |
| POST | `/api/auth/login` | 없음 |
| POST | `/api/auth/logout` | Bearer |
| GET | `/api/servers` | Bearer |
| GET | `/api/servers/best` | Bearer |
| POST | `/api/servers/register` | `X-Server-Key` |
| POST | `/api/servers/heartbeat` | `X-Server-Key` |
| POST | `/api/servers/unregister` | `X-Server-Key` |
| GET | `/health` | 없음 |

> 로그인은 아이디·비밀번호 **형식만** 검사하는 데모입니다. 실제 서비스 적용 전 확인할 점은
> [../Docs/ARCHITECTURE.md](../Docs/ARCHITECTURE.md#실서비스-전에-반드시-바꿔야-할-것)에 정리해두었습니다.
