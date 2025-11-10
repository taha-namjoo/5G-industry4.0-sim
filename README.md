# 5G-industry4.0-sim

This repository provides setup instructions for deploying **OAI CN5G (Core Network 5G)** components using Docker.  
Follow the steps below to prepare, configure, and run your 5G core environment.

---

## 1. OAI CN5G

### 1.1 OAI CN5G Pre-requisites

Install the required dependencies:

```bash
sudo apt install -y git net-tools putty
```

Then install **Docker** and related components:

```bash
# Install Docker
# Reference: https://docs.docker.com/engine/install/ubuntu/
sudo apt update
sudo apt install -y ca-certificates curl
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}") stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
```

Add your current user to the Docker group (to avoid using `sudo` for Docker commands):

```bash
sudo usermod -a -G docker $(whoami)
reboot
```

---

### 1.2 OAI CN5G Configuration Files

Download and extract the OAI CN5G configuration files:

```bash
wget -O ~/oai-cn5g.zip https://gitlab.eurecom.fr/oai/openairinterface5g/-/archive/develop/openairinterface5g-develop.zip?path=doc/tutorial_resources/oai-cn5g
unzip ~/oai-cn5g.zip
mv ~/openairinterface5g-develop-doc-tutorial_resources-oai-cn5g/doc/tutorial_resources/oai-cn5g ~/oai-cn5g
rm -r ~/openairinterface5g-develop-doc-tutorial_resources-oai-cn5g ~/oai-cn5g.zip
```

> ⚙️ **Note:**  
> After downloading, make sure to **replace** the default `config.yaml` and `oai_db.sql` files inside `~/oai-cn5g` with your customized versions.

---

### 1.3 Pull OAI CN5G Docker Images

Pull the OAI CN5G Docker images:

```bash
cd ~/oai-cn5g
docker compose pull
```

---

### 1.4 Run OAI CN5G

Start OAI CN5G:

```bash
cd ~/oai-cn5g
docker compose up -d
```

---

### 1.5 Stop OAI CN5G

Stop OAI CN5G:

```bash
cd ~/oai-cn5g
docker compose down
```

---

## 📘 Notes

- Ensure Docker is running before executing any `docker compose` commands.
- Replace configuration files carefully to avoid conflicts.
- This setup is based on the official [OpenAirInterface 5G Core Network](https://gitlab.eurecom.fr/oai/openairinterface5g) documentation.

---


---

# 2. Deployment

## 2.1 OAI RAN

The **OAI E2 Agent** includes:

- **RAN functions**: exposure of service model capabilities  
- **FlexRIC submodule**: for message encoding/decoding

### Currently available versions

| Protocol | Version | Supported in OAI | Supported in FlexRIC |
|-----------|----------|------------------|----------------------|
| E2SM-KPM | v2.03 | ✅ | ✅ |
| E2SM-KPM | v3.00 | ✅ | ✅ |
| E2AP | v1.01 | ✅ | ✅ |
| E2AP | v2.03 | ✅ (default) | ✅ |
| E2AP | v3.01 | ✅ | ✅ |

> **Note:**  
> E2SM-KPM v2.01 is supported only in **FlexRIC**, but **not** in OAI.

---

### 2.1.1 Clone the OAI repository

```bash
git clone https://gitlab.eurecom.fr/oai/openairinterface5g
```

---

### 2.1.2 Build OAI with E2 Agent

Using the `build_oai` script:

```bash
cd openairinterface5g/cmake_targets/
./build_oai -I  # Run this once before the first build to install dependencies
./build_oai --gNB --nrUE --build-e2 --cmake-opt -DE2AP_VERSION=E2AP_VX --cmake-opt -DKPM_VERSION=KPM_VY --ninja
```

Where `X = 1, 2, 3` and `Y = 2_03, 3_00`.

**Example:**

```bash
./build_oai --gNB --nrUE --build-e2 --cmake-opt -DE2AP_VERSION=E2AP_V2 --cmake-opt -DKPM_VERSION=KPM_V2_03 --ninja
```

#### Build Options Explained

- `-I` → Install pre-requisites (only needed the first time or if dependencies change)  
- `-w` → Select radio head support (e.g., hardware or simulator)  
- `--gNB` → Build NR softmodem and CU-UP components  
- `--nrUE` → Build NR UE softmodem  
- `--ninja` → Use the Ninja build tool for faster compilation  
- `--build-e2` → Enable the E2 Agent integration within E2 nodes (gNB-mono, DU, CU, CU-UP, CU-CP)

> The RF simulator can be built using the `-w SIMU` option, but it is also built by default during any softmodem build.

---

## 2.2 FlexRIC

By default, **FlexRIC** builds the nearRT-RIC with **E2AP v2** and **KPM v2**.  
If you need a different version, edit the variables `E2AP_VERSION` and `KPM_VERSION` inside FlexRIC’s `CMakeLists.txt` file.

> ⚠️ **Important:**  
> The `E2AP_VERSION` and `KPM_VERSION` used by **OAI** and **FlexRIC** must match due to O-RAN version incompatibilities.

---

### 2.2.1 Clone the FlexRIC repository

```bash
git clone https://gitlab.eurecom.fr/mosaic5g/flexric flexric
cd flexric/
```

---

### 2.2.2 Build FlexRIC

```bash
mkdir build && cd build && cmake .. && make -j8
```

---

### 2.2.3 Install Service Models (SMs)

```bash
sudo make install
```

By default, the service model libraries will be installed to:

```
/usr/local/lib/flexric
```

and the configuration file to:

```
/usr/local/etc/flexric
```

## 🧾 License

This repository follows the licensing terms of the original OAI CN5G components.  
For detailed licensing information, please refer to the [OAI GitLab repository](https://gitlab.eurecom.fr/oai/openairinterface5g).

---

© 2025 – 5G-industry4.0-sim Project

