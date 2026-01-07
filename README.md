# 🕒 OLED 시계 디스플레이
*Linux Kernel Device Driver 기반 OLED 시계 디스플레이*

## 📌 프로젝트 개요
Raspberry Pi 환경에서 I2C / SPI 통신과 GPIO 인터럽트를 직접 제어하여 RTC 기반 OLED 시계 디스플레이 시스템를 구현한 프로젝트입니다.
커널 레벨에서 하드웨어를 직접 제어하고, User Space 애플리케이션과의 연계를 통해 시간 표시·입력 처리·상태 제어까지 포함한 임베디드 시스템을 구현하는 것을 목표로 하였습니다.

- I2C / SPI 프로토콜 구조 및 동작 원리 이해
- GPIO 기반 RTC(DS1302)를 커널 드라이버에서 직접 제어
- Rotary Encoder 및 버튼 입력을 인터럽트 기반으로 처리
- Kernel ↔ User Space 연계 구조 설계 및 구현
---
## 🧩 하드웨어 구성
<img width="500" height="359" alt="image" src="https://github.com/user-attachments/assets/c2f650c3-3dfd-4fb9-b50d-11e5ff0f931a" />
<br>
</br>
<img width="800" height="500" alt="image" src="https://github.com/user-attachments/assets/f533b4aa-2348-41f3-9ae5-500c1af5c3db" />

| 구성 요소                 | 인터페이스            | 역할            |
| --------------------- | ---------------- | ------------- |
| Raspberry Pi          | GPIO / I2C / SPI | 전체 시스템 제어     |
| SSD1306 OLED (I2C)    | I2C              | 시간 정보 출력      |
| SSD1306 OLED (SPI)    | SPI              | 통신 방식 비교용 출력  |
| RTC DS1302            | GPIO             | 실시간 날짜·시간 제공  |
| Rotary Encoder (RV09) | GPIO (Interrupt) | 시간 설정 및 메뉴 입력 |
| Push Button           | GPIO             | 모드 전환 / 확인 입력 |

---
## 🏗 소프트웨어 아키텍처
<img width="1020" height="547" alt="image" src="https://github.com/user-attachments/assets/6dd343c9-cbfe-4605-a3fe-61a090318f2c" />

| 계층           | 구성             | 설명                      |
| ------------ | -------------- | ----------------------- |
| User Space   | app.c          | `/dev` 디바이스 접근 및 UI 제어  |
| Kernel Space | dev.c          | Character Device Driver |
| 통신 방식        | poll()         | 이벤트 발생 시 즉시 처리          |
| 입력 처리        | GPIO Interrupt | 로터리 엔코더/버튼 입력           |
| 출력 제어        | I2C / SPI      | OLED 디스플레이 제어           |

---
### 상태전이 구조
<img width="800" height="571" alt="image" src="https://github.com/user-attachments/assets/cf98c5e8-4f33-4c30-b1fe-4252f5855905" />

| 상태           | 설명             |
| ------------ | -------------- |
| CLOCK        | 기본 시계 표시 화면    |
| SETTING      | 로터리 엔코더로 시간 설정 |
| SCREEN SAVER | 일정 시간 무입력 시 진입 |

---
## ⚙ 주요 기능
| 기능         | 설명                        |
| ---------- | ------------------------- |
| RTC 시간 표시  | DS1302 RTC에서 시간 정보 주기적 읽기 |
| OLED 이중 출력 | I2C OLED / SPI OLED 동시 제어 |
| 시간 설정 UI   | 로터리 엔코더 회전/클릭으로 시간 조정     |

---
## 🔬 요소기술
<img width="800" height="586" alt="image" src="https://github.com/user-attachments/assets/89b0facd-2786-4cae-81c9-06b283fc4d17" />
<img width="800" height="586" alt="image" src="https://github.com/user-attachments/assets/b85a934f-2cb8-497d-916f-217bd7b89ba3" />

| 요소기술          | 적용 내용            | 특징                   |
| ------------- | ---------------- | -------------------- |
| I2C           | OLED(I2C) 제어     | 2선식, 주소 기반, ACK/NACK |
| SPI           | OLED(SPI) 제어     | 고속 통신, CPOL/CPHA 설정  |
| GPIO          | RTC·입력 장치 제어     | 커널에서 직접 제어           |
| Interrupt     | 로터리 엔코더 입력       | 실시간 반응, CPU 효율       |
| poll()        | 이벤트 감시           | 다중 디바이스 확장에 유리       |
| Kernel Driver | Character Device | User ↔ Kernel 인터페이스  |

