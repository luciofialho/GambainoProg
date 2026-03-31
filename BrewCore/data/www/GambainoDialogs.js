function menuLineCIP() {
  new $.Zebra_Dialog(
    `
    <div style="text-align:center">
      <div style="margin-top:10px">
        <button class="dlg-btn-large" onclick="selectLineCIP(6)">Passthrough rinse</button>
        <button class="dlg-btn-large" onclick="selectLineCIP(300)">Boiling water</button><br><br>
        <button class="dlg-btn-large" onclick="selectLineCIP(311)">Alkaline detergent</button>
        <button class="dlg-btn-large" onclick="selectLineCIP(321)">Acid detergent</button><br><br>
        <button class="dlg-btn-large" onclick="selectLineCIP(312)">Alkaline detergent<br>+ pot soak</button>
        <button class="dlg-btn-large" onclick="selectLineCIP(322)">Acid detergent<br>+ pot soak</button><br><br>
      </div>
    </div>
    `,
    {
      title: "Line CIP Selection",
      width: 600,
      buttons: false,
      type: false
    }
  );
}

function selectLineCIP(cipType) {
  // fecha todos os diálogos
  $('.ZebraDialog').remove();
  $('.ZebraDialogBackdrop').remove();
  
  startProgram(cipType);
}


function menuFMTCIP() {
  new $.Zebra_Dialog(
    `
    <div style="text-align:center">
      <div style="margin-top:10px">
        <button class="dlg-btn-large" onclick="selectFMTCIP('One phase cold')">One phase cold</button>
        <button class="dlg-btn-large" onclick="selectFMTCIP('One phase warm')">One phase warm</button><br><br>
        <button class="dlg-btn-large" onclick="selectFMTCIP('Two phase warm+cold')">Two phase warm+cold</button>
        <button class="dlg-btn-large" onclick="selectFMTCIP('Two phase cold+cold')">Two phase cold+cold</button><br><br>
        <button class="dlg-btn-large" onclick="selectFMTCIP('Rinse only')">Rinse only</button>
        <button class="dlg-btn-large" onclick="selectFMTCIP('Manual control')">Manual control</button>
      </div>
    </div>
    `,
    {
      title: "FMT CIP Selection",
      width: 600,
      buttons: false,
      type: false
    }
  );
}

function selectFMTCIP(cipType) {
  // fecha todos os diálogos
  $('.ZebraDialog').remove();
  $('.ZebraDialogBackdrop').remove();
  
  if (cipType === "One phase cold") {
    startProgram(25); // ajuste o número do programa conforme necessário
  } else if (cipType === "One phase warm") {
    startProgram(26);
  } else if (cipType === "Two phase warm+cold") {
    startProgram(1127);
  } else if (cipType === "Two phase cold+cold") {
    startProgram(28);
  } else if (cipType === "Rinse only") {
    startProgram(29);
  } else if (cipType === "Manual control") {
    startProgram(30);
  }
}

function menuKegClean() {
  new $.Zebra_Dialog(
    `
    <div style="text-align:center">
      <div style="margin-top:10px">
        <button class="dlg-btn-large" onclick="selectKegClean('Detergent full cleaning')">Detergent full cleaning</button>
        <button class="dlg-btn-large" onclick="selectKegClean('Detergent full cleaning no setup')">Detergent full cleaning<br>no setup</button><br><br>
        <button class="dlg-btn-large" onclick="selectKegClean('Detergent pre cleaning')">Detergent pre cleaning</button>
        <button class="dlg-btn-large" onclick="selectKegClean('Detergent pre cleaning no setup')">Detergent pre cleaning<br>no setup</button><br><br>
        <button class="dlg-btn-large" onclick="selectKegClean('Rinse only')">Rinse only</button>
        <button class="dlg-btn-large" onclick="selectKegClean('Rinse only no setup')">Rinse only<br>no setup</button><br><br>
      </div>
    </div>
    `,
    {
      title: "Keg Clean Selection",
      width: 600,
      buttons: false,
      type: false
    }
  );
}

function selectKegClean(cleaningType) {
  // fecha todos os diálogos
  $('.ZebraDialog').remove();
  $('.ZebraDialogBackdrop').remove();
  
  if (cleaningType === "Detergent full cleaning") {
    startProgram(1201); 
  } else if (cleaningType === "Detergent full cleaning no setup") {
    startProgram(1202);
  } else if (cleaningType === "Detergent pre cleaning") {
    startProgram(1211);
  } else if (cleaningType === "Detergent pre cleaning no setup") {
    startProgram(1212);
  } else if (cleaningType === "Rinse only") {
    startProgram(1221);
  } else if (cleaningType === "Rinse only no setup") {
    startProgram(1422);
  }
}




function menuFlowMeterCalibration() {
  new $.Zebra_Dialog(
    `
    <div style="text-align:center">
      <div style="margin-top:10px">
        <button class="dlg-btn-large" onclick="selectFlowMeterCalibration('Cold water 1')">Cold water 1</button>
        <button class="dlg-btn-large" onclick="selectFlowMeterCalibration('Cold water 2')">Cold water 2</button><br><br>
        <button class="dlg-btn-large" onclick="selectFlowMeterCalibration('Hot water')">Hot water</button>
        <button class="dlg-btn-large" onclick="selectFlowMeterCalibration('Cold water with pumps')">Cold water with pumps</button>
      </div>
    </div>
    `,
    {
      title: "Flow meter calibration",
      width: 600,
      buttons: false,
      type: false
    }
  );
}

function selectFlowMeterCalibration(waterType) {
  // fecha todos os diálogos
  $('.ZebraDialog').remove();
  $('.ZebraDialogBackdrop').remove(); 
  if (waterType === "Cold water 1") {
    startProgram(18);
  } else if (waterType === "Cold water 2") {
    startProgram(19);
  } else if (waterType === "Hot water") {
    startProgram(20);
  } else if (waterType === "Cold water with pumps") {
    startProgram(21);
  }
}

function menuDiagram() {
  new $.Zebra_Dialog(
    `
    <div style="text-align:center">
      <div style="margin-top:10px">
        <button class="dlg-btn-large" onclick="selectDiagram(1)">Pre Brew</button>
        <button class="dlg-btn-large" onclick="selectDiagram(2)">Infusion</button><br><br>
        <button class="dlg-btn-large" onclick="selectDiagram(3)">Brew</button>
        <button class="dlg-btn-large" onclick="selectDiagram(4)">Brew Top-up</button><br><br>
        <button class="dlg-btn-large" onclick="selectDiagram(5)">Rinse</button>
        <button class="dlg-btn-large" onclick="selectDiagram(6)">Detergent</button><br><br>
        <button class="dlg-btn-large" onclick="selectDiagram(7)">FMT CIP</button>
        <button class="dlg-btn-large" onclick="selectDiagram(8)">Calibration</button><br><br>
        <button class="dlg-btn-large" onclick="selectDiagram(-327)">Reset</button>
      </div>
    </div>
    `,
    {
      title: "Line Configuration",
      width: 600,
      buttons: false,
      type: false
    }
  );
}

function selectDiagram(lineConfig) {
  // fecha todos os diálogos
  $('.ZebraDialog').remove();
  $('.ZebraDialogBackdrop').remove();
  
  // Envia o comando para alterar a configuração da linha
  WriteParam("LineConfiguration", lineConfig);
}

function menuDiagnostics() {
  new $.Zebra_Dialog(
    `
    <div style="text-align:left; padding: 20px">
      <h3>Diagnostic parameters</h3>
      <div style="margin-bottom:15px; text-align:center">
        <button style="padding:5px 15px; margin:0 5px" onclick="toggleAllDiagParams(true)">Select all</button>
        <button style="padding:5px 15px; margin:0 5px" onclick="toggleAllDiagParams(false)">Deselect all</button>
      </div>
      <form id="diagForm" style="font-size:18px; line-height:2.2">
        <label style="display:block; cursor:pointer"><input type="checkbox" name="param" value="1" checked style="transform:scale(1.5); margin-right:10px; vertical-align:middle"> Valves actuators (current and time)</label>
        <label style="display:block; cursor:pointer"><input type="checkbox" name="param" value="2" checked style="transform:scale(1.5); margin-right:10px; vertical-align:middle"> Valves (flow) - circuit 1</label>
        <label style="display:block; cursor:pointer"><input type="checkbox" name="param" value="4" checked style="transform:scale(1.5); margin-right:10px; vertical-align:middle"> Valves (flow) - circuit 2</label>
        <label style="display:block; cursor:pointer"><input type="checkbox" name="param" value="8" checked style="transform:scale(1.5); margin-right:10px; vertical-align:middle"> Valves (flow) - circuit 3</label>
        <label style="display:block; cursor:pointer"><input type="checkbox" name="param" value="16" checked style="transform:scale(1.5); margin-right:10px; vertical-align:middle"> Valves (flow) - FMT side</label>
        <label style="display:block; cursor:pointer"><input type="checkbox" name="param" value="32" checked style="transform:scale(1.5); margin-right:10px; vertical-align:middle"> Pumps and flow meters</label>
        <label style="display:block; cursor:pointer"><input type="checkbox" name="param" value="64" checked style="transform:scale(1.5); margin-right:10px; vertical-align:middle"> MLT level sensors</label>
        <label style="display:block; cursor:pointer"><input type="checkbox" name="param" value="128" checked style="transform:scale(1.5); margin-right:10px; vertical-align:middle"> BK level sensors</label>
      </form>
      <div style="margin-top:20px; text-align:center">
        <button style="padding:8px 20px; margin:0 5px; font-size:14px" onclick="executeDiag()">Execute</button>
        <button style="padding:8px 20px; margin:0 5px; font-size:14px" onclick="window.location.href=URLArduino + 'diagresult'">Last result</button>
      </div>
    </div>
    `,
    {
      title: "Diagnostic Parameters",
      width: 600,
      buttons: false,
      type: false
    }
  );
}

function toggleAllDiagParams(checked) {
  var checkboxes = document.getElementsByName("param");
  for (var i = 0; i < checkboxes.length; i++) {
    checkboxes[i].checked = checked;
  }
}

function executeDiag() {
  var sum = 0;
  // Recupera todos os checkboxes com o nome "param"
  var checkboxes = document.getElementsByName("param");
  for (var i = 0; i < checkboxes.length; i++) {
    if (checkboxes[i].checked) {
      sum += parseInt(checkboxes[i].value);
    }
  }
  
  // Fecha o diálogo
  $('.ZebraDialog').remove();
  $('.ZebraDialogBackdrop').remove();
  callGambainoJSON("startdiag_" + sum);
  //window.location.href = URLArduino + "startdiag_" + sum;
}

