#include "BMW-I3-HTML.h"
#include "BMW-I3-BATTERY.h"
#include "../datalayer/datalayer_extended.h"

// Helper function to get DTC description
static String getDTCDescription(uint32_t code) {
  switch (code) {
    case 0xCAD421:
      return "HV: General fault";
    case 0xCAD422:
      return "HV: Comm fault";
    case 0xCAD423:
      return "HV: Voltage fault";
    case 0xCAD424:
      return "HV: Temp fault";
    case 0xCAD425:
      return "HV: Current fault";
    case 0xCAD426:
      return "HV: Isolation fault";
    case 0xCAD427:
      return "HV: Contactor fault";
    case 0xCAD428:
      return "HV: Cell voltage fault";
    case 0xCAD429:
      return "HV: Internal comm fault";
    case 0xCAD42A:
      return "HV: Cooling fault";
    default:
      return "";
  }
}

// Helper function for safe array access
static const char* safeArrayAccess(const char* const arr[], size_t arrSize, int idx) {
  if (idx >= 0 && static_cast<size_t>(idx) < arrSize) {
    return arr[idx];
  }
  return "Unknown";
}

String BmwI3HtmlRenderer::get_status_html() {
  String content;

  content += "<h4>SOC raw: " + String(batt.SOC_raw()) + "</h4>";
  content += "<h4>SOC dash: " + String(batt.SOC_dash()) + "</h4>";
  content += "<h4>SOC OBD2: " + String(batt.SOC_OBD2()) + "</h4>";
  static const char* statusText[16] = {
      "Not evaluated", "OK", "Error!", "Invalid signal", "", "", "", "", "", "", "", "", "", "", "", ""};
  content += "<h4>Interlock: " + String(safeArrayAccess(statusText, 16, batt.ST_interlock())) + "</h4>";
  content += "<h4>Isolation external: " + String(safeArrayAccess(statusText, 16, batt.ST_iso_ext())) + "</h4>";
  content += "<h4>Isolation internal: " + String(safeArrayAccess(statusText, 16, batt.ST_iso_int())) + "</h4>";
  content += "<h4>Isolation: " + String(safeArrayAccess(statusText, 16, batt.ST_isolation())) + "</h4>";
  content += "<h4>Cooling valve: " + String(safeArrayAccess(statusText, 16, batt.ST_valve_cooling())) + "</h4>";
  content += "<h4>Emergency: " + String(safeArrayAccess(statusText, 16, batt.ST_EMG())) + "</h4>";
  static const char* prechargeText[16] = {"Not evaluated",
                                          "Not active, closing not blocked",
                                          "Error precharge blocked",
                                          "Invalid signal",
                                          "",
                                          "",
                                          "",
                                          "",
                                          "",
                                          "",
                                          "",
                                          "",
                                          "",
                                          "",
                                          "",
                                          ""};
  content += "<h4>Precharge: " + String(safeArrayAccess(prechargeText, 16, batt.ST_precharge())) +
             "</h4>";  //Still unclear of enum
  static const char* DCSWText[16] = {"Contactors open",
                                     "Precharge ongoing",
                                     "Contactors engaged",
                                     "Invalid signal",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     ""};
  content += "<h4>Contactor status: " + String(safeArrayAccess(DCSWText, 16, batt.ST_DCSW())) + "</h4>";
  static const char* contText[16] = {"Contactors OK",
                                     "One contactor welded!",
                                     "Two contactors welded!",
                                     "Invalid signal",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     "",
                                     ""};
  content += "<h4>Contactor weld: " + String(safeArrayAccess(contText, 16, batt.ST_WELD())) + "</h4>";
  static const char* valveText[16] = {"OK",
                                      "Short circuit to GND",
                                      "Short circuit to 12V",
                                      "Line break",
                                      "",
                                      "",
                                      "Driver error",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "",
                                      "Stuck",
                                      "Stuck",
                                      "",
                                      "Invalid Signal"};
  content +=
      "<h4>Cold shutoff valve: " + String(safeArrayAccess(valveText, 16, batt.ST_cold_shutoff_valve())) + "</h4>";

  // Get DTC data for this specific battery instance
  DATALAYER_INFO_BMWI3* dtc = batt.get_dtc_data();

  // DTC (Diagnostic Trouble Codes) Section
  content += "<h3 style='color: #ff9800; border-bottom: 2px solid #ff9800; padding-bottom: 5px;'>⚠️ Diagnostic Trouble "
             "Codes</h3>";
  content += "<div style='margin-left: 15px;'>";

  if (dtc->dtc_count == 0 && !dtc->dtc_read_in_progress && !dtc->dtc_read_failed &&
      dtc->dtc_last_read_millis == 0) {
    content += "<h4>DTCs not read yet. Use 'Read DTC' button to check for faults.</h4>";
  } else if (dtc->dtc_read_in_progress) {
    content += "<h4>DTC read in progress...</h4>";
  } else if (dtc->dtc_read_failed) {
    content += "<h4 style='color: #f44336;'>Last DTC read failed. Battery may have rejected the request.</h4>";
  } else if (dtc->dtc_count == 0) {
    content += "<h4 style='color: #4caf50;'>✓ No DTCs present - Battery health OK</h4>";
  } else {
    // Show last read time
    unsigned long timeSinceRead = millis() - dtc->dtc_last_read_millis;
    unsigned long seconds = timeSinceRead / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;

    content += "<h4>Last DTC read: ";
    if (days > 0) {
      content += String(days) + "d ";
    }
    content += String(hours % 24) + "h " + String(minutes % 60) + "m " + String(seconds % 60) + "s ago</h4>";

    content += "<h4>Active DTCs: " + String(datalayer_extended.bmwi3.dtc_count) + "</h4>";

    // Display DTCs in a table
    content += "<table style='width: 100%; border-collapse: collapse; margin-top: 10px;'>";
    content += "<tr style='background-color: #424242;'>";
    content += "<th style='padding: 8px; border: 1px solid #555;'>DTC Code</th>";
    content += "<th style='padding: 8px; border: 1px solid #555;'>Status</th>";
    content += "<th style='padding: 8px; border: 1px solid #555;'>Description</th>";
    content += "</tr>";

    for (int i = 0; i < dtc->dtc_count && i < 32; i++) {
      content += "<tr>";

      // DTC Code (hex format)
      content += "<td style='padding: 8px; border: 1px solid #555;'>0x";
      char codeStr[7];
      snprintf(codeStr, sizeof(codeStr), "%06X", (unsigned int)dtc->dtc_codes[i]);
      content += String(codeStr);
      content += "</td>";

      // Status (decode status byte)
      uint8_t status = dtc->dtc_status[i];
      content += "<td style='padding: 8px; border: 1px solid #555;'>";

      String statusColor;
      String statusText;

      if (status & 0x01) {  // Active fault (bit 0)
        statusColor = "#d32f2f";
        statusText = "Active";
      } else if (status & 0x08) {  // Confirmed fault (bit 3)
        statusColor = "#ff6f00";
        statusText = "Confirmed";
      } else {
        statusColor = "#757575";
        statusText = "Stored";
      }

      content += "<span style='color: " + statusColor + "; font-weight: bold;'>" + statusText + "</span>";
      content += "</td>";

      // Description
      content += "<td style='padding: 8px; border: 1px solid #555;'>";
      String desc = getDTCDescription(dtc->dtc_codes[i]);
      if (desc.length() > 0) {
        content += desc;
      } else {
        content += "Unknown DTC";
      }
      content += "</td>";

      content += "</tr>";
    }

    content += "</table>";
  }

  content += "</div>";

  return content;
}
