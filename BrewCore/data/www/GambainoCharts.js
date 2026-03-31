GambainoStorage = window.localStorage;
var chart_Big;
var chart_Small;



function setupCharts() {

  chart_Big = new Chart(document.getElementById('bigChartCanvas').getContext('2d'),
    {
      type: 'line',
      options: {
        responsive: true,
        maintainAspectRatio: false,
        title: {
          display: false,
        },
        plugins: {
          legend: {
            position: 'top',
          },
        },
        animation: false,
        layout: {
          padding: { top:0, bottom:0, left:0, right:0 }
        },
        scales: {
          x: { ticks: { padding: 2 } },
          y: { ticks: { padding: 4 } }
        }
      }
    }
  );
  chart_Small = new Chart(document.getElementById('smallChartCanvas').getContext('2d'),
    {
      type: 'line',
      options: {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
          legend: {
            display: false,
          },
        },
        scales: {
          y: {
            position: 'right'
          }
        },
        animation:false,
        layout: {
          padding: { top:0, bottom:0, left:0, right:0 }
        },
        scales: {
          x: { ticks: { padding: 2 } },
          y: {
            position: 'right',
            ticks: { padding: 4 }
          }
        }
      }
    }
  );
}

function updateCharts() {
  var show = graphType != 0;
  document.getElementById("bigChart-container").style.visibility   = show ? "visible" : "hidden";
  document.getElementById("smallChart-container").style.visibility = show ? "visible" : "hidden";
  if (show) {
    //chart_Big.setJSONData(chartDataBig());
    
    var x= chartData(true);
    chart_Small.data.labels = x.labels;
    chart_Small.data.datasets = x.datasets;
    if (AV["Status"] == STTRAN) {
      chart_Small.options.plugins.annotation = {
        annotations: {
        line1: {
          type: 'line',
          mode: 'horizontal',
          yMin: AV["RcpInoculationTemp"],
          yMax: AV["RcpInoculationTemp"],
          borderColor: 'green',
          borderWidth: 2
        }
      }
      };
    }
    else
      chart_Small.options.plugins.annotation = {};

chart_Small.update();    

    chart_Small.update();

    x = chartData(false);
    chart_Big.data.labels = x.labels;
    chart_Big.data.datasets = x.datasets;
    chart_Big.update();
  }
}


function lastValues(s,n) {
  if (n==0)
    return s.slice(1);
  else {
    count = 0;
    for (i = s.length-1; i>=0; i--) {
      if (s[i]==',') {
        count++;
        if (count==n) 
          return s.slice(i+1);
      }
    }
    return s.slice(1);
  }
}

function chartData(doSmall) {
    lastLength = GambainoStorage.getItem("TimeLabel").length;
    sliceSize = 5;
    effectiveSize = (doSmall ? 20 : 0);

    Xaxis   = '[' + lastValues(GambainoStorage.getItem("TimeLabel"),effectiveSize)+ ']';
    dataarray=[];
    switch (graphType) {
      case 1:
        MLTT    = '{   "label":"MLT temp"  , "borderColor":"#e44a00",   "borderWidth":"3", "pointRadius": "0", "pointStyle":false,                        "data": ['+lastValues(GambainoStorage.getItem("MLTTemp"),effectiveSize) + ' ] }';
        MLTTT   = '{   "label":"MLT target", "borderColor":"#e44a00",   "borderWidth":"1", "pointRadius": "0", "pointStyle":false, "borderDash" : [10,5], "data": ['+lastValues(GambainoStorage.getItem("MLTTargetTemp"),effectiveSize) + ' ] }';
        MLTTopT = '{   "label":"MLT top"   , "borderColor":"#e44a00",   "borderWidth":"1", "pointRadius": "0", "pointStyle":false, "borderDash" : [5,1],  "data": ['+lastValues(GambainoStorage.getItem("MLTTopTemp"),effectiveSize) + ' ] }';
        if (!doSmall) {
          HLTT  = '{   "label":"HLT temp"  , "borderColor":"#6060FFAA", "borderWidth":"2", "pointRadius": "0", "pointStyle":false,                        "data": ['+lastValues(GambainoStorage.getItem("HLTTemp"),effectiveSize) + ' ] }';
          HLTTT = '{   "label":"HLT target", "borderColor":"#6060FF",   "borderWidth":"1", "pointRadius": "0", "pointStyle":false, "borderDash" : [10,5], "data": ['+lastValues(GambainoStorage.getItem("HLTTargetTemp"),effectiveSize) + ' ] }';
        }

        if (!doSmall) {
          dataarray=[JSON.parse(MLTT),
                    JSON.parse(MLTTT),
                    JSON.parse(MLTTopT),
                    JSON.parse(HLTT),
                    JSON.parse(HLTTT)];
        }
        else {
          dataarray=[JSON.parse(MLTT),
                    JSON.parse(MLTTT),
                    JSON.parse(MLTTopT)];
        }
        break;
    
    case 2:
      BKT     = '{   "label":"BK temp"   , "borderColor":"#e44a00",   "borderWidth":"3", "pointRadius": "0", "pointStyle":false,                        "data": ['+lastValues(GambainoStorage.getItem("BKTemp"),effectiveSize) + ' ] }';
      BKTT    = '{   "label":"BK target" , "borderColor":"#e44a00",   "borderWidth":"1", "pointRadius": "0", "pointStyle":false, "borderDash" : [10,5], "data": ['+lastValues(GambainoStorage.getItem("BKTargetTemp"),effectiveSize) + ' ] }';
      dataarray = [JSON.parse(BKT), JSON.parse(BKTT)];
      break;

    case 3:
    Chiller2T = '{   "label":"Chiller 2" , "borderColor":"#e44a00CC",   "borderWidth":"2", "pointRadius": "0", "pointStyle":false,                      "data": ['+lastValues(GambainoStorage.getItem("Chiller2Temp"),effectiveSize) + ' ] }';
    TransferT = '{   "label":"Average"   , "borderColor":"#6060FF",   "borderWidth":"3", "pointRadius": "0", "pointStyle":false,                        "data": ['+lastValues(GambainoStorage.getItem("TransferAvgTemp"),effectiveSize) + ' ] }';
    FermenterT= '{   "label":"Fermenter" , "borderColor":"#e44a00",   "borderWidth":"3", "pointRadius": "0", "pointStyle":false,                        "data": ['+lastValues(GambainoStorage.getItem("FermenterTemp"),effectiveSize) + ' ] }';
    if (!doSmall) {
        WasteT    = '{   "label":"Waste"     , "borderColor":"#808000",   "borderWidth":"1", "pointRadius": "0", "pointStyle":false,                      "data": ['+lastValues(GambainoStorage.getItem("WasteTemp"),effectiveSize) + ' ] }';
        dataarray = [JSON.parse(Chiller2T), JSON.parse(WasteT), JSON.parse(TransferT), JSON.parse(FermenterT)];
    }
    else 
      dataarray = [JSON.parse(Chiller2T), JSON.parse(TransferT), JSON.parse(FermenterT)];
      
      break;
    }

    return {
      "labels": JSON.parse(Xaxis),
      "datasets": dataarray
    }
}

function ProcessGraphStorage(timeLabel,
                             mltTemp,mltTopTemp,mltTargetTemp,hltTemp,hltTargetTemp,
                             bkTemp,bkTargetTemp,
                             chiller2Temp,wasteTemp,avgTransferTemp,fermenterTemp) {
  function fV(Value) {
    if (Value>100)
      return ',100';
    else if (Value>0)
      return ','+Value;
    else
      return ',null'
  }
  
  GambainoStorage.setItem('TimeLabel'    ,GambainoStorage.getItem('TimeLabel')+',"'+timeLabel+'"');
  switch (graphType) {
    case 1:
      GambainoStorage.setItem('MLTTemp'      ,GambainoStorage.getItem('MLTTemp')      +fV(mltTemp));
      GambainoStorage.setItem('MLTTopTemp'   ,GambainoStorage.getItem('MLTTopTemp')   +fV(mltTopTemp));
      GambainoStorage.setItem('MLTTargetTemp',GambainoStorage.getItem('MLTTargetTemp')+fV(mltTargetTemp));
      GambainoStorage.setItem('HLTTemp'      ,GambainoStorage.getItem('HLTTemp')      +fV(hltTemp));
      GambainoStorage.setItem('HLTTargetTemp',GambainoStorage.getItem('HLTTargetTemp')+fV(hltTargetTemp));
      break;
    case 2:
      GambainoStorage.setItem('BKTemp'       ,GambainoStorage.getItem('BKTemp')       +fV(bkTemp));
      GambainoStorage.setItem('BKTargetTemp' ,GambainoStorage.getItem('BKTargetTemp') +fV(bkTargetTemp));
      break;
   case 3:
      GambainoStorage.setItem('Chiller2Temp'   ,GambainoStorage.getItem('Chiller2Temp')    +fV(chiller2Temp));
      GambainoStorage.setItem('WasteTemp'      ,GambainoStorage.getItem('WasteTemp')       +fV(wasteTemp));
      GambainoStorage.setItem('TransferAvgTemp',GambainoStorage.getItem('TransferAvgTemp') +fV(avgTransferTemp));
      GambainoStorage.setItem('FermenterTemp'  ,GambainoStorage.getItem('FermenterTemp')   +fV(fermenterTemp));
      break;
  }
};

function processChartsSeries()  {
  var d = new Date(); var n = d.getTime();
  if (n > lastGraph + GraphUpdateInterval) {
      lastGraph = n;
      if (GambainoStorage.getItem("MLTTemp").length > 65000) // should not happen
        resetGraph();
      processGraph = true;
      if (AV["Status"]>=STMINMLT && AV["Status"]<=STMAXMLT) {
        graphType = 1;
        GraphUpdateInterval = 10000; 
      }
      else if ((AV["Status"]>=STMINBK1 && AV["Status"]<=STMAXBK1) || (AV["Status"]>=STMINBK2 && AV["Status"]<=STMAXBK2)) {
        graphType=2;
        GraphUpdateInterval = 10000; 
      }
      else if (AV["Status"]==STTRAN) {
        graphType=3;
        GraphUpdateInterval = 2000;
      }
      else 
        graphType = 0;

    
      if (graphType!=GambainoStorage.getItem("lastGraphType")) {
        resetGraph();
        GambainoStorage.setItem("lastGraphType",graphType);
      }

      var timeStamp = d.getHours()+':' + (d.getMinutes()<10 ? "0" : "")+d.getMinutes();
      switch (graphType) {
        case 1: ProcessGraphStorage(timeStamp,
                                    AV["MLTTemp"],
                                    AV["MLTTopTemp"],
                                    AV["MLTTargetTemp"],
                                    AV["HLTTemp"],
                                    AV["HLTTargetTemp"],
                                    0,0,
                                    0,0,0,0);
                break;
        case 2: ProcessGraphStorage(timeStamp,
                                    0,0,0,0,0,
                                    AV["BKTemp"],
                                    AV["BKTargetTemp"],
                                    0,0,0,0);
                break;
        case 3: ProcessGraphStorage(timeStamp,
                                    0,0,0,0,0,
                                    0,0,
                                    AV["Chiller2Temp"],
                                    AV["Chiller2WasteTemp"],
                                    AV["TransferAvgTemp"],
                                    AV["FermenterTemp"]);
                break;
          
        default:
          processGraph = false;
      }                      
  }
}

function resetGraph()
{
  GambainoStorage.clear();
  GambainoStorage.setItem("lastGraphType","0");
  GambainoStorage.setItem("MLTTemp","");
  GambainoStorage.setItem("MLTTopTemp","");
  GambainoStorage.setItem("MLTTargetTemp","");
  GambainoStorage.setItem("HLTTemp","");
  GambainoStorage.setItem("HLTTargetTemp","");
  GambainoStorage.setItem("BKTemp","");
  GambainoStorage.setItem("BKTargetTemp","");
  GambainoStorage.setItem("Chiller2Temp","");
  GambainoStorage.setItem("WasteTemp","");
  GambainoStorage.setItem("TransferAvgTemp","");
  GambainoStorage.setItem("FermenterTemp","");
  GambainoStorage.setItem("TimeLabel","");
}

lastGraph = 0;
graphType=0;
GambainoStorage = window.localStorage;
if (GambainoStorage.getItem("lastGraphType")   ==null) GambainoStorage.setItem("lastGraphType","0");
if (GambainoStorage.getItem("MLTTemp")         ==null) GambainoStorage.setItem("MLTTemp","");
if (GambainoStorage.getItem("MLTTopTemp")      ==null) GambainoStorage.setItem("MLTTopTemp","");
if (GambainoStorage.getItem("MLTTargetTemp")   ==null) GambainoStorage.setItem("MLTTargetTemp","");
if (GambainoStorage.getItem("HLTTemp")         ==null) GambainoStorage.setItem("HLTTemp","");
if (GambainoStorage.getItem("HLTTargetTemp")   ==null) GambainoStorage.setItem("HLTTargetTemp","");
if (GambainoStorage.getItem("BKTemp")          ==null) GambainoStorage.setItem("BKTemp","");
if (GambainoStorage.getItem("BKTargetTemp")    ==null) GambainoStorage.setItem("BKTargetTemp","");
if (GambainoStorage.getItem("Chiller2Temp")    ==null) GambainoStorage.setItem("Chiller2Temp","");
if (GambainoStorage.getItem("WasteTemp")       ==null) GambainoStorage.setItem("WasteTemp","");
if (GambainoStorage.getItem("TransferAvgTemp") ==null) GambainoStorage.setItem("TransferAvgTemp","");
if (GambainoStorage.getItem("FermenterTemp")   ==null) GambainoStorage.setItem("FermenterTemp","");
if (GambainoStorage.getItem("TimeLabel")       ==null) GambainoStorage.setItem("TimeLabel","");

