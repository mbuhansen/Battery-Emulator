import re
import csv

def parse_bmw_i3_log(log_path, output_csv):
    latest_state = {
        'timestamp': '00:00:00',
        'raw_soc': None,
        'display_soc_pct': None,
        'max_current_a': None,
        'max_power_w': None,
        'abort_flag': None,
    }
    
    records = []
    seen_rows = set()
    
    print("Behandler logfil... Vent venligst.")
    
    with open(log_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line_num, line in enumerate(f, 1):
            if not line.strip() or line.startswith('Received') or line.startswith('PP2CAN'):
                continue
            
            # Format: >  linje_nr  timestamp  St  CAN_ID  ...
            parts = line.split()
            if len(parts) < 8:
                continue
                
            try:
                # Spring startkarakter over hvis den indeholder '>'
                offset = 1 if parts[0].startswith('>') else 0
                
                linje_id = parts[offset]
                timestamp = parts[offset + 1]
                can_id = parts[offset + 3].upper()
                dlc = int(parts[offset + 5])
                data_bytes = parts[offset + 6 : offset + 6 + dlc]
                
                # 0x112: Rå HV-batteri SOC
                if can_id == '0X112' and dlc >= 6:
                    byte4, byte5 = int(data_bytes[4], 16), int(data_bytes[5], 16)
                    latest_state['raw_soc'] = ((byte5 & 0x0F) << 8) | byte4
                
                # 0x432: Dashboard SOC
                elif can_id == '0X432' and dlc >= 5:
                    byte4 = int(data_bytes[4], 16)
                    latest_state['display_soc_pct'] = byte4 / 2.0
                
                # 0x2F5: Mulige opladningsgrænser (Ladestrøm)
                elif can_id == '0X2F5' and dlc >= 4:
                    byte2, byte3 = int(data_bytes[2], 16), int(data_bytes[3], 16)
                    raw_current = (byte3 << 8) | byte2
                    latest_state['max_current_a'] = (raw_current - 8192) / 10.0
                
                # 0x40D: Langtidseffekt tilladt
                elif can_id == '0X40D' and dlc >= 6:
                    byte4, byte5 = int(data_bytes[4], 16), int(data_bytes[5], 16)
                    latest_state['max_power_w'] = ((byte5 << 8) | byte4) * 3
                
                # 0x431: BMS Abort Flag
                elif can_id == '0X431' and dlc >= 1:
                    byte0 = int(data_bytes[0], 16)
                    latest_state['abort_flag'] = (byte0 & 0x30) >> 4
                
                # For at tracke ændringer og gemme et repræsentativt udsnit
                # Vi gemmer hvis der sker en ændring i en af de vigtige parametre
                state_key = (
                    latest_state['raw_soc'],
                    latest_state['display_soc_pct'],
                    latest_state['max_current_a'],
                    latest_state['max_power_w'],
                    latest_state['abort_flag']
                )
                
                if state_key not in seen_rows and None not in state_key:
                    seen_rows.add(state_key)
                    records.append({
                        'linje': line_num,
                        'timestamp': timestamp,
                        'raw_soc_ticks': latest_state['raw_soc'],
                        'display_soc_pct': latest_state['display_soc_pct'],
                        'max_current_a': latest_state['max_current_a'],
                        'max_power_w': latest_state['max_power_w'],
                        'abort_flag': latest_state['abort_flag'],
                    })
            except Exception as e:
                continue

    # Sorter efter linjenummer
    records.sort(key=lambda x: x['linje'])

    # Skriv resultater til CSV
    with open(output_csv, 'w', newline='', encoding='utf-8') as csvfile:
        fieldnames = ['linje', 'timestamp', 'raw_soc_ticks', 'display_soc_pct', 'max_current_a', 'max_power_w', 'abort_flag']
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)
        
    print(f"Fuldført! {len(records)} unikke tilstande skrevet til {output_csv}")

if __name__ == '__main__':
    log_file = r"c:\Users\micha\Downloads\Battery-Emulator-9.2.1\Battery-Emulator-9.2.1\Software\end_charging_batt_car.log"
    out_csv = r"c:\Users\micha\Downloads\Battery-Emulator-9.2.1\Battery-Emulator-9.2.1\Software\charging_progression.csv"
    parse_bmw_i3_log(log_file, out_csv)
