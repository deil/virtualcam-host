# Meh Virtual Webcam

### **Architectural Summary**

The project is a **cross-platform virtual webcam** that allows you to use your phone's camera as a high-quality video source on your computer. It is implemented through a client-server architecture consisting of a native mobile app (client) and a lightweight desktop app (server), designed to operate exclusively on a local network for maximum performance and privacy.

### **1. Desktop Application (Server)**

This lightweight server application runs on the user's computer. Its purpose is to receive the video stream and create the virtual webcam device.

*   **Framework:** **Electron**
*   **UI:** HTML, CSS, JavaScript/TypeScript
*   **Core Logic:** A **Native Node.js Addon (in C++)** will be created to handle all high-performance media tasks.

### **2. Mobile Application (Client)**

This app runs on the user's phone, acting as the video source.

*   **Framework:** **Kotlin/Compose Multiplatform**
*   **Function:** Captures video from the phone's camera and streams it via WebRTC to the desktop server.

### **3. Video Streaming & Virtual Camera (The Native Addon)**

This is the high-performance engine inside the desktop application. It's a single, unified native module that handles the entire video pipeline.

*   **Core Technology:** **GStreamer** (embedded within the C++ addon).
*   **Architecture:** A GStreamer pipeline will be constructed in the C++ code to manage the WebRTC stream and output to the virtual camera.
*   **Local Network Constraint:** The WebRTC connection is strictly confined to the local network. This **eliminates the need for external STUN or TURN servers**, simplifying the configuration and guaranteeing that video data never leaves the user's LAN.

### **4. Signaling and Discovery**

This subsystem enables the desktop server and mobile client to find each other automatically and negotiate the local video connection.

*   **Discovery Technology:** **mDNS** (Bonjour/Zeroconf).
*   **Signaling Technology:** **WebSockets**.
*   **Workflow:**
    1.  The apps use mDNS to automatically discover each other's local IP address and port.
    2.  A WebSocket connection is established directly between the two local IPs.
    3.  The WebRTC session negotiation occurs over this WebSocket. Crucially, **only local network IP addresses (e.g., `192.168.x.x`) are exchanged as ICE candidates**, forcing the video stream to stay on the high-speed local network.
*   **Libraries:**
    *   **Desktop:** `bonjour-service` and `ws` npm packages.
    *   **Android:** Native `NsdManager` API.
    *   **iOS:** Native `Network` framework (Bonjour).
