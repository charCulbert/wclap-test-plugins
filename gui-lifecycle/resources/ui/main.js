const status = document.querySelector("#status");
const send = command => parent.postMessage(new Uint8Array([command]).buffer, "*");

addEventListener("message", event => {
	const bytes = new Uint8Array(event.data);
	if (bytes.length > 1) {
		status.textContent = bytes[1] ? "Host accepted." : "Host rejected.";
	}
	else if (bytes.length) send(bytes[0]);
});

requestAnimationFrame(async () => {
	const styled = getComputedStyle(document.body)
		.getPropertyValue("--wclap-resources").trim() === "ready";
	const message = await fetch("message.txt").then(response => response.text());
	if (styled && message.trim() === "Multi-file resources ready.") send(1);
});
