const status = document.querySelector("#status");
const send = command => parent.postMessage(new Uint8Array([command]).buffer, "*");
let resend = 0;

addEventListener("message", event => {
	const bytes = new Uint8Array(event.data);
	if (bytes.length > 1) {
		clearInterval(resend);
		status.textContent = bytes[1] ? "Host accepted." : "Host rejected.";
	}
	else if (bytes.length) send(bytes[0]);
});

// The host may deliver the first message before the plug-in takes messages,
// so the ready message is sent again every 250 ms until a reply arrives.
requestAnimationFrame(async () => {
	const styled = getComputedStyle(document.body)
		.getPropertyValue("--wclap-resources").trim() === "ready";
	const message = await fetch("message.txt").then(response => response.text());
	if (!styled || message.trim() !== "Multi-file resources ready.") return;
	send(1);
	resend = setInterval(() => send(1), 250);
});
