if (URLArduino==null) {
  console.log("Development mode");
  var URLArduino = "http://192.168.29.178/";
}
else
  console.log("Operational mode");


// graph is sensitive to status - these are the boundaries
    STMINMLT  =12;   STMAXMLT  =16;  // from infusion to PrepSparge
    STMINBK1  =17;   STMAXBK1  =20;  // from sparge to KS_Whirlpool
    STMINBK2  =25;   STMAXBK2  =30;  // from waitboil to Whirlpoolrest

    STTRAN    =31;                 // transfer

try {STMINMLT = STMINMLT_;} catch {}
try {STMAXMLT = STMAXMLT_;} catch {}
try {STMINBK1 = STMINBK1_;} catch {}
try {STMAXBK1 = STMAXBK1_;} catch {}
try {STMINBK2 = STMINBK2_;} catch {}
try {STMAXBK2 = STMAXBK2_;} catch {}
try {STTRAN = STTRAN_;}     catch {}



processGraph=false;
diagramIsOverrided = false;

var oldAV;
var noOH = -327;
var GraphUpdateInterval = 10000;
var lastGraphLoad = 0; // counts for graph reload

var AsyncLock = 0;
var AV='';
var JSON_Todo='';
var CommandInQueue='';

function secondsToHMS(s) {
    if (s<0) {
        sgn = "- ";//"<font color='#BB0000'>- ";
        s = -s;
    }
    else
        sgn = "";
    var h = Math.floor(s/3600); //Get whole hours
    s -= h*3600;
    var m = Math.floor(s/60); //Get remaining minutes
    s -= m*60;
    return sgn+(h < 10 ? '0'+h : h)+":"+(m < 10 ? '0'+m : m)+":"+(s < 10 ? '0'+s : s); //zero padding on minutes and seconds
}

function breakHM(v) {
    x = 0;
    y = 0;
    sg = 1;
    s=" "+v;
    for (i=0;i<s.length;i++) {
        if (s[i]=='-')
            sg = -sg;
        if (s[i] >= '0' && s[i] <= '9')
            x = 10*x+1*s[i];
        if (s[i]==':') {
            y = y*60 + x;
            x = 0;
        }
    }
    x = sg*(x+60*y);
    return x;
}


function callGambaino(str) {
  var url = URLArduino + str;
  var request = new XMLHttpRequest();
  request.open("GET", url, false);
  request.send(null);  
    return;
}

function callGambainoJSON(str) {
  var url = URLArduino + str;
  var request = new XMLHttpRequest();
  request.open("GET", url, false);
  request.send(null); 
  return JSON.parse(request.responseText);
}

function callGambainoJSONAsync(str="gj_1") {
    if (AsyncLock>0) {
        AsyncLock ++;
        if (AsyncLock < 5) {
          console.log("Incrementing Asynclock: " + AsyncLock+ "  str: "+str);
          if (str!="gj_1") {
              //console.log("saved command: "+str);
              CommandInQueue = str;
          }
          return;
        }
        console.log("Missed response: recovered from lost AsyncLock:" + AsyncLock);
    }
    
    AsyncLock = 1;    
    CommandInQueue = "";

    var url = URLArduino + str;
    var request = new XMLHttpRequest();    
    request.timeout = 5000;

    request.onload = function() {
        //console.log("Load");
        AsyncLock = 0;
        if(request.readyState==4)
          if (request.status==200) {
			  resposta = request.responseText;
			  if (resposta.charAt(0)=='[' || resposta.substring(resposta.indexOf('{'))>0) {
				  resposta = resposta.substring(resposta.indexOf('{'));
				  }
			  if (resposta.substring(resposta.indexOf('['))>0)
				  resposta= resposta.substring(0,resposta.indexOf('['));
			  try{
              AV = JSON.parse(resposta);
        }
			  catch {
			      console.log("Response parser error: \n" +resposta);
			  }
              JSON_Todo = AV["Todo"];
              ProcessRenderAll();
          }
          else
              content = '';
            
        if (CommandInQueue!="") {
          //console.log("Recursed for queued command: "+CommandInQueue);
          callGambainoJSONAsync(CommandInQueue);
        }
            
    };
    request.ontimeout = function() {
        console.log("Timed out in " +request + " time: "+request.timeLabel);
        AsyncLock = 0;
    };
  
    request.open("GET", url, true); 
    request.send(null);  
}


function Action(ActionName) {
  callGambaino(ActionName);
  RenderAll();  
}

function startProgram(pgm) {
  if (pgm != 0 && confirm("Start program?")) {
    Action('StartProgram_'+pgm);
    resetGraph();
    ControlButtonVisibility(0);
  }
  else if (pgm == 0 && confirm("Stop program?")) {
    Action('StartProgram_'+pgm);
    resetGraph();
    ControlButtonVisibility(0);
  }
}


// Read & write procVar parameters 

function WriteParam(name, value) {
  if (name=="TopUpWaterHeater")
    name = "TopUpHeater";
  callGambainoJSONAsync(".fo_" + name + "_" + value);
}

// Status controls 

function GetStatusList() {
  var JSON_StatusList = callGambainoJSON("statuslist");
  document.getElementById("StatusList").innerHTML = "";

  for(var i = 0,j=0; i <= Object.keys(JSON_StatusList).length-1; j++) {
    x = JSON_StatusList[j];
    if (typeof(x) != "undefined") {
      document.getElementById("StatusList").innerHTML = document.getElementById("StatusList").innerHTML + "<option value='" + j + "'> "+ x +"</option>";
      i++;
    }
  }
}

function GetIPCStatusList() {
  var JSON_IPCStatusList = callGambainoJSON("ipcstatuslist");
  document.getElementById("IPCStatusList").innerHTML = "";

  for(var i = 0,j=0; i <= Object.keys(JSON_IPCStatusList).length-1; j++) {
    x = JSON_IPCStatusList[j];
    if (typeof(x) != "undefined") {
      document.getElementById("IPCStatusList").innerHTML = document.getElementById("IPCStatusList").innerHTML + "<option value='" + j + "'> "+ x +"</option>";
      i++;
    }
  }
}

function ChangeStatus() {
  var Status = document.getElementById("StatusList");
  callGambaino(".fo_Status_"+ Status.options[Status.selectedIndex].value);
  //RenderAll();  
}

function ChangeIPCStatus() {
  var IPCStatus = document.getElementById("IPCStatusList");
  callGambaino(".fo_IPCStatus_"+ IPCStatus.options[IPCStatus.selectedIndex].value);
  //RenderAll();
}

// To Do List Control 

function WriteTodoListTable() {
  //callGambainoJSONAsync("ToDo", "tj");
    //if (JSON_Todo == '') 
    //    return;
    has = false;
    
  var content = "<div class='TodoHeader'>Tasks</div>";
  jQuery.each(JSON_Todo,
    function(i, v) { 
        has= true;
    var style='xx';
    switch(v.Status) {
      case "W": 
        style='TodoSleepingItem';
        break;      
      case "O": 
        style='TodoOverdueItem';
        break;      
      case "S": 
        style='TodoStandbyItem';
        break;      
      case "A": 
        style='TodoNormalItem';
        break;      
    };
    content += "<div class='"+style+"' onClick='DismissTodo("+v.ID+")'> <br>"+v.Name+" </div>";
    });

    if (has) {
        document.getElementById("divTodoList").innerHTML = content;
        document.getElementById("divTodoList").style.visibility = "visible";
    }
    else {
        document.getElementById("divTodoList").innerHTML = "";
        document.getElementById("divTodoList").style.visibility = "hidden";
    }        
}

function DismissTodo(ID) {
    if(confirm('Dismiss?')) {
    callGambaino("dt_" + ID);
    RenderAll();
    }
}

// Override procVar parameters 

function Override(div) {
  v = div.id;

  var overrideValue = prompt(v);
  
  if (overrideValue != null) {
    if (overrideValue != "") {
      WriteParam(v, overrideValue);
      RenderAll();
    }
    else {
      WriteParam(v, noOH);
      RenderAll();
    }
  };
};

function OverrideAB(div) {
  new $.Zebra_Dialog("Choose override for " + div.id,
  { type: "question",
    title: div.id,
    buttons:["port A", "port B", "No OH"],
    onClose: function(caption) {
      if (caption=="port A") {
        WriteParam(div.id, 0);
        RenderAll();
      }
      else if (caption=="port B") {
        WriteParam(div.id, 1);
        RenderAll();
      }
      else if (caption=="No OH") {
        WriteParam(div.id, noOH);
        RenderAll();
      };
    }
  });
}


function OverrideOnOff(div) {
  new $.Zebra_Dialog("Choose override for " + div.id,
  { type: "question",
    title: div.id,
    buttons:["Off", "On", "No OH"],
    onClose: function(caption) {
      if (caption=="Off") {
        WriteParam(div.id, 0);
        RenderAll();
      }
      else if (caption=="On") {
        WriteParam(div.id, 1);
        RenderAll();
      }
      else if (caption=="No OH") {
        WriteParam(div.id, noOH);
        RenderAll();
      };
    }
  });
}

function OverrideOpenClosed(div) {
  new $.Zebra_Dialog("Choose override for " + div.id,
  { type: "question",
    title: div.id,
    buttons:["Closed", "Open", "No OH"],
    onClose: function(caption) {
      if (caption=="Closed") {
        WriteParam(div.id, 0);
        RenderAll();
      }
      else if (caption=="Open") {
        WriteParam(div.id, 1);
        RenderAll();
      }
      else if (caption=="No OH") {
        WriteParam(div.id, noOH);
        RenderAll();
      };
    }
  });
}

function OverrideOpenClosed(div) {
  new $.Zebra_Dialog("Choose override for " + div.id,
  { type: "question",
    title: div.id,
    buttons:["Closed", "Open", "No OH"],
    onClose: function(caption) {
      if (caption=="Closed") {
        WriteParam(div.id, 0);
        RenderAll();
      }
      else if (caption=="Open") {
        WriteParam(div.id, 1);
        RenderAll();
      }
      else if (caption=="No OH") {
        WriteParam(div.id, noOH);
        RenderAll();
      };
    }
  });
}


function OverrideBK(div) {
  new $.Zebra_Dialog("Choose override for " + div.id,
  { type: "question",
    title: div.id,
    buttons:["Off", "1/3", "2/3", "Max", "No OH"],
    width: 510,
    onClose: function(caption) {
      if (caption=="Off") {
        WriteParam(div.id, 0);
        RenderAll();
      }
      if (caption=="1/3") {
        WriteParam(div.id, 1);
        RenderAll();
      }
      else if (caption=="2/3") {
        WriteParam(div.id, 2);
        RenderAll();
      }
      else if (caption=="Max") {
        WriteParam(div.id, 3);
        RenderAll();
      }
      else if (caption=="No OH") {
        WriteParam(div.id, noOH);
        RenderAll();
      };
    }
  });
}



function OverrideHM(div) {
  var overrideValue = prompt("Enter time in status in HH:MM:SS format");
  if (overrideValue != null) {
    if (overrideValue != "") {
            value = breakHM(overrideValue);
      WriteParam(div.id, value);
      RenderAll();
    }
  };
} 





function RenderComponent(Id, Value, OH) {
  var ComponentType;
  if (Id!="" && (ComponentType = document.getElementById(Id).className) != "") {
    if (OH == 1) 
      document.getElementById(Id).style.border = "1px solid rgba(255, 0, 0, 1)";
    else if (OH == 0) 
      document.getElementById(Id).style.border = "1px solid rgba(0, 0, 0, 0)";

    if (Id=="LineConfiguration") {
      diagramIsOverrided = OH;
    }

  
    switch(ComponentType) {
      case "Pump":
        if (Value > 0)
          document.getElementById(Id).style.backgroundImage = "url(./img/pump_on.gif)";
        else 
          document.getElementById(Id).style.backgroundImage = "url(./img/pump_off.png)";
        break;

        
      case "Chiller":
        if (Value > 0) 
          document.getElementById(Id).style.backgroundImage = "url(./img/Chiller_on.gif)";
        else
          document.getElementById(Id).style.backgroundImage = "url(./img/Chiller_off.gif)";
        break;

      case "ColdBankControl":
        if (Value > 0) 
          document.getElementById(Id).style.backgroundImage = "url(./img/ColdBank_on.gif)";
        else
          document.getElementById(Id).style.backgroundImage = "url(./img/ColdBank_off.gif)";
        break;        

      case "TopLevel":
              if (Value==0) document.getElementById(Id).style.backgroundImage = "url(./img/Top_0.gif)";
              if (Value==1) document.getElementById(Id).style.backgroundImage = "url(./img/Top_1.gif)";
              if (Value==2) document.getElementById(Id).style.backgroundImage = "url(./img/Top_2.gif)";
                  
        break;
      case "HLTBKLevel":
              document.getElementById(Id).style.backgroundImage = "url(./img/BKLevel_"+Math.trunc(Value)+".png)";
              //if (Value==1) document.getElementById(Id).style.backgroundImage = "url(./img/Top_1.gif)";
              //if (Value==2) document.getElementById(Id).style.backgroundImage = "url(./img/Top_2.gif)";
                  
        break;
        
      case "Valve2":
        if (Value > 0) 
          document.getElementById(Id).style.backgroundImage = "url(./img/valve_open.png)";
        else 
          document.getElementById(Id).style.backgroundImage = "url(./img/valve_closed.png)";
        break;

      case "Drain":
        if (Value > 0) 
          document.getElementById(Id).style.backgroundImage = "url(./img/drain_open.png)";
        else 
          document.getElementById(Id).style.backgroundImage = "url(./img/drain_closed.png)";
        break;

        
      case "Valve3": 
        if (Value == 0) 
          document.getElementById(Id).style.backgroundImage = "url(./img/"+Id+"_A.png)";
        else if (Value == 1) 
          document.getElementById(Id).style.backgroundImage = "url(./img/"+Id+"_B.png)";
        break;
        
      case "Thermometer":
      case "ThermometerVert":
        if (Value>-127)
          document.getElementById(Id).innerHTML = "&emsp;&nbsp;"+Value + "c";
        break;

      case "Flow":
        document.getElementById(Id).innerHTML = Value + " l/m";
        break;

      case "Volume":
      case "VolumeFMT":
        document.getElementById(Id).innerHTML = "&emsp;&nbsp;"+Value + "L"
        break;

        
      case "Heater":
        if (Id!='BKHeater') Value = 3*Value;
        document.getElementById(Id).style.backgroundImage = "url(./img/Heater_"+Math.trunc(Value)+".png)";
        break;
                  
      case "TargetTemp":
        if (Id=="MLTTargetTemp")
          if (Value==0)
            document.getElementById(Id).style.visibility = "hidden";
          else 
            document.getElementById(Id).style.visibility = "visible";

        if (Value<=-6)
          document.getElementById(Id).innerHTML = "N/H";
        else if (Value>900)
          document.getElementById(Id).innerHTML = "Max";
        else if (Value>100)
          document.getElementById(Id).innerHTML = "Boil";
        else
          document.getElementById(Id).innerHTML = Value;// + "c";
        break;

      case "TargetLevel":
        if (Value>-5)
          document.getElementById(Id).innerHTML = Value;// + " L";
        else
          document.getElementById(Id).innerHTML = "N/I";
          
        break;

      case "RemainingVolume":
        if (Value==0)
          document.getElementById(Id).innerHTML = "- - -";
        else
          document.getElementById(Id).innerHTML = Value;
        break;

      case "Text":
        document.getElementById(Id).innerHTML = Value;
        break;            
        
      case "StatusName":
      case "SubStatusName":
        if (Value!="---") {
          document.getElementById(Id).innerHTML = Value;
          document.getElementById(Id).style.visibility='visible';
        }
        else {
          document.getElementById(Id).style.visibility='hidden';
          document.getElementById(Id).innerHTML = "";
        }
        break;            
        
      case "ConsoleMessage":
        document.getElementById(Id).innerHTML = Value;
        break;            
        
      case "IPCStatusName":
        if (Value=='---') {
          document.getElementById(Id).style.visibility='hidden';
          document.getElementById(Id).innerHTML = "";
        }
        else {
          document.getElementById(Id).style.visibility='visible';
          document.getElementById(Id).innerHTML = "IPC: " + Value;
        }
        break;            
          
      case "TimeInStatus":
        document.getElementById(Id).innerHTML = Math.floor(Value);
        break;

      case "Diagram":
          
          //document.getElementById(Id).style.height = '411px';
          //document.getElementById("LineCharts").style.visibility = AV["Status"] != 0 ? "visible" : "hidden";
          /*
          if (AV["Status"] == STTRAN) {
            TransferVolume.style.visibility = "visible";         
            document.getElementById("TransferFlowBig").style.visibility   = "visible";
            document.getElementById("TransferFlow").style.visibility = "hidden";
          }
            */

          switch (parseInt(Value)) {
            case 1:  document.getElementById(Id).style.backgroundImage ="url(./img/1_PreBrew.png)";       break; // pode colocar quantos backgrounds quiser, separados por virgula caso queira economizar espaço     , url(./img/1_PreBrew.png)"; break;
            case 2:  document.getElementById(Id).style.backgroundImage ="url(./img/2_Infusion.png)";      break;
            case 3:  document.getElementById(Id).style.backgroundImage ="url(./img/3_brew.png)";          break;
            case 4:  document.getElementById(Id).style.backgroundImage ="url(./img/4_BrewTopup.png)";     break;
            case 5:  document.getElementById(Id).style.backgroundImage ="url(./img/5_LineRinse.png)";     break;
            case 6:  document.getElementById(Id).style.backgroundImage ="url(./img/6_LineDetergent.png)"; break;
            case 7:  document.getElementById(Id).style.backgroundImage ="url(./img/7_FMTCIP.png)";        break;            
            case 8:  document.getElementById(Id).style.backgroundImage ="url(./img/8_Calibration.png)";      break;
          }
/*        
          document.getElementById("FMTDrain").style.visibility = (!isSmall) ? "visible" : "hidden";
          document.getElementById("FMTWaterIn").style.visibility = (!isSmall) ? "visible" : "hidden";
          document.getElementById("FMTCycle").style.visibility = (!isSmall) ? "visible" : "hidden";
          document.getElementById("FMTPump").style.visibility = (!isSmall) ?  "visible" : "hidden";

          document.getElementById("Chiller2Temp").style.visibility = parseInt(Value) != 7 ? "visible" : "hidden";          

          document.getElementById("TopUpWaterHeater").style.visibility = parseInt(Value) == 1 ? "visible" : "hidden";
          document.getElementById("TopUpWaterTemp").style.visibility = parseInt(Value) == 1 ? "visible" : "hidden";          
          */
          break;
          
          
      case "WaterIntake":
          if (Value>0)
            document.getElementById(Id).style.backgroundImage = "url(./img/Water_1.gif)"
          else
            document.getElementById(Id).style.backgroundImage = "url(./img/Water_0.gif)";
        break;
      case "HotWaterIntake":
          if (Value>0)
            document.getElementById(Id).style.backgroundImage = "url(./img/HotWater_1.gif)"
          else
            document.getElementById(Id).style.backgroundImage = "url(./img/HotWater_0.gif)";
          break;
      case "O3":
          if (Value>0)
            document.getElementById(Id).style.backgroundImage = "url(./img/O3_1.gif)"
          else
            document.getElementById(Id).style.backgroundImage = "url(./img/O3_0.gif)";
          break;
    }    
  }
}


  

function RenderAll() {
    callGambainoJSONAsync();
}


var lastStatus;

function ProcessRenderAll() {
  processChartsSeries();
   
  var ComponentDivs = document.getElementsByTagName("*");
  //getElementsByClassName(ComponentClasses[i]);

  chiller2Status = 1;
  topupheaterStatus = 0;

  forceDiagram = AV['Status'] != lastStatus;
  if (forceDiagram) {
    lastStatus = AV['Status'];
    console.log("Status changed to: "+lastStatus);
  }
  
  for (var j = 0; j <= ComponentDivs.length-1; j++) {
    cid = ComponentDivs[j].id;
    if (cid=="TopUpWaterHeater")
      cid = "TopUpHeater";

    if (cid!='') {
      cv  = AV[cid];
      if (cv)
        if ((! window.oldAV) || (window.oldAV[cid] != cv) || (cid=="LineConfiguration" && forceDiagram) || (window.oldAV[cid+'_O'] != AV[cid+'_O'])) {
          var oh = 0;
          if (AV[cid+'_O'] == 1)
            oh = 1;
          RenderComponent(ComponentDivs[j].id, cv , oh);
        }
      if ((cid=="ColdBankPump" && cv==0) || (cid=="ColdBankValve" && cv==0))
        chiller2Status = 0;
      if (cid=="TopUpWaterHeater" && AV['TopUpHeater']>0)
        topupheaterStatus = 3;
    }
    RenderComponent("Chiller2", chiller2Status,0);
  }

  window.oldAV = AV;
  WriteTodoListTable();
  time = Math.round(AV["TimeInStatus"]);
  document.getElementById("TimeInStatus").innerHTML = secondsToHMS(time);
    
};


function GetMaxLevel(PotName) {
  var MaxLevel = "50";
  switch (PotName) {
  case "MLT":
    MaxLevel = 114;
    break;
  case "HLT":
    MaxLevel = 114;  
    break;
  case "BK":
    MaxLevel = 114;  
    break;
  }
  return MaxLevel;
}



function ControlButtonVisibility(visible) {
  if (visible) {
    controlDiv.style.visibility='visible'; 
    ControlButton.innerHTML='Control &#x219f';
  }
  else {
    controlDiv.style.visibility='hidden';
    ControlButton.innerHTML='Control &#x21a1';
  } 

  closeNav('BrewMenu');
  closeNav('CIPMenu');
  closeNav('ToolsMenu');
  closeNav('ControlMenu');
  
  statusSelection.style.display='none';


  if (AV['Status']==0)  {
    SaveOption.style.display = 'none';
    ResumeOption.style.display = '';
    //ProgramOptions.style.display = '';
    StopProgramOption.style.display = 'none';    
        //btnSave.innerHTML = "Resume";
  }
  else {
    SaveOption.style.display = '';
    ResumeOption.style.display = 'none';
    //ProgramOptions.style.display = 'none';
    StopProgramOption.style.display = '';
  }
   // btnSave.innerHTML = "Save";
}