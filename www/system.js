var svg;
var config;
var state;
var requestCirc;
function initSystem()
{
	var svgo = document.getElementById('system'); 
	svg = svgo.contentDocument;
	svg.getElementById('circ').addEventListener("click", function() {
		svg.getElementById('circ').style.fill='#800'; 
		requestCirc = true;
		$.ajax({url: "/api/settings/dhw/request", type: "PUT", contentType: "application/json", data: 'true'
		}).done(function() {
			svg.getElementById('circ-arrow').style.fill= '#400'; 
		});
	});
	poll();
}
function poll() {
    $.ajax({
        url: "/api/status",
        type: "GET",
        dataType: "json",
        complete: setTimeout(function() {poll()}, 2000),
        timeout: 2000
    }).done(function(data) {
		state = data;
		if (state["temperature"] !== undefined && state["pump"] !== undefined) {
			svg.getElementById('pump-loader').style.fill= state.pump.water ? '#0f0' :'#fff'; 
			svg.getElementById('pump-c1').style.fill= state.pump.flow ? '#0f0' :'#fff'; 
			svg.getElementById('pump-circ').style.fill= state.pump.circulation ? '#0f0' :'#fff'; 
			svg.getElementById('outdoor-temp').textContent = state.temperature.outdoor+'\xb0'; 
			svg.getElementById('dhw-temp').textContent = state.temperature.dhw+'\xb0'; 
			svg.getElementById('exhaust-temp').textContent = state.temperature.exhaust+'\xb0'; 
			svg.getElementById('boiler-temp').textContent = state.temperature.boiler+'\xb0'; 
			if (state.pump.circulation && requestCirc) {
				requestCirc = false;
				svg.getElementById('circ-arrow').style.fill='#000'; 
			}
		}
	});
}
