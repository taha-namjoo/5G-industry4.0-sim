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

## 2. Deployment

### 2.1 OAI RAN

The **OAI E2 Agent** includes:

- **RAN functions**: exposure of service model capabilities  
- **FlexRIC submodule**: for message encoding/decoding

#### Currently available versions

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

#### 2.1.1 Clone the OAI repository

```bash
git clone https://gitlab.eurecom.fr/oai/openairinterface5g
```

---

#### 2.1.2 Build OAI with E2 Agent

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

##### Build Options Explained

- `-I` → Install pre-requisites (only needed the first time or if dependencies change)  
- `-w` → Select radio head support (e.g., hardware or simulator)  
- `--gNB` → Build NR softmodem and CU-UP components  
- `--nrUE` → Build NR UE softmodem  
- `--ninja` → Use the Ninja build tool for faster compilation  
- `--build-e2` → Enable the E2 Agent integration within E2 nodes (gNB-mono, DU, CU, CU-UP, CU-CP)

> The RF simulator can be built using the `-w SIMU` option, but it is also built by default during any softmodem build.

---

### 2.2 FlexRIC

By default, **FlexRIC** builds the nearRT-RIC with **E2AP v2** and **KPM v2**.  
If you need a different version, edit the variables `E2AP_VERSION` and `KPM_VERSION` inside FlexRIC's `CMakeLists.txt` file.

> ⚠️ **Important:**  
> The `E2AP_VERSION` and `KPM_VERSION` used by **OAI** and **FlexRIC** must match due to O-RAN version incompatibilities.

---

#### 2.2.1 Clone the FlexRIC repository

```bash
git clone https://gitlab.eurecom.fr/mosaic5g/flexric flexric
cd flexric/
```

---

#### 2.2.2 Build FlexRIC

```bash
mkdir build && cd build && cmake .. && make -j8
```

---

#### 2.2.3 Install Service Models (SMs)

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

---

## 3. Start the Process

At this point, we assume the **5G Core Network** is already running in the background. For more information, please follow the **5GCN tutorial**.

Optionally, run **Wireshark** and capture E2AP traffic.

---

### 3.1 E2 Node Configuration

The E2 node (gNB-mono, DU, CU, CU-UP, CU-CP) configuration file must contain the `e2_agent` section. Please adjust the following example to your needs:

```conf
e2_agent = {
  near_ric_ip_addr = "127.0.0.1";
  sm_dir = "/usr/local/lib/flexric/";
}
```

---

### 3.2 UE ID Representation

As per **O-RAN.WG3.E2SM-v02.00** specifications, UE ID (section 6.2.2.6) representation in OAI is:

| Component | CHOICE UE ID case | AMF UE NGAP ID | GUAMI | gNB-CU UE F1AP ID | gNB-CU-CP UE E1AP ID | RAN UE ID |
|-----------|-------------------|----------------|-------|-------------------|----------------------|-----------|
| **gNB-mono** | GNB_UE_ID_E2SM | amf_ue_ngap_id | guami | - | - | rrc_ue_id |
| **CU** | GNB_UE_ID_E2SM | amf_ue_ngap_id | guami | rrc_ue_id | - | rrc_ue_id |
| **CU-CP** | GNB_UE_ID_E2SM | amf_ue_ngap_id | guami | rrc_ue_id | rrc_ue_id | rrc_ue_id |
| **CU-UP** | GNB_CU_UP_UE_ID_E2SM | - | - | - | rrc_ue_id | rrc_ue_id |
| **DU** | GNB_DU_UE_ID_E2SM | - | - | - | - | rrc_ue_id |

---

### 3.3 Start the E2 Nodes

#### 3.3.1 Start the gNB-mono

```bash
cd <path-to>/build
sudo ./nr-softmodem -O ../../../targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb.sa.band78.fr1.106PRB.usrpb210.conf --gNBs.[0].min_rxtxtime 6 --rfsim
```

---

#### 3.3.2 Start gNB with CU/DU Split

If **CU/DU split** is used, start the gNB as follows:

```bash
cd <path-to>/build
sudo ./nr-softmodem -O <path-to>/targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb-du.sa.band78.106prb.rfsim.pci0.conf --rfsim
sudo ./nr-softmodem -O <path-to>/targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb-cu.sa.f1.conf
```

---

#### 3.3.3 Start gNB with CU-CP/CU-UP/DU Split

If **CU-CP/CU-UP/DU split** is used, start the gNB as follows:

```bash
cd <path-to>/build
sudo ./nr-softmodem -O <path-to>/targets/PROJECTS/GENERIC-NR-5GC/CONF/gnb-du.sa.band78.106prb.rfsim.pci0.conf --rfsim
./nr-softmodem -O <path-to>/ci-scripts/conf_files/gnb-cucp.sa.f1.conf --gNBs.[0].plmn_list.[0].mcc 001 --gNBs.[0].plmn_list.[0].mnc 01 --gNBs.[0].local_s_address "127.0.0.3" --gNBs.[0].amf_ip_address.[0].ipv4 "192.168.70.132" --gNBs.[0].E1_INTERFACE.[0].ipv4_cucp "127.0.0.3" --gNBs.[0].NETWORK_INTERFACES.GNB_IPV4_ADDRESS_FOR_NG_AMF "192.168.70.129" --e2_agent.near_ric_ip_addr "127.0.0.1" --e2_agent.sm_dir "/usr/local/lib/flexric/"
sudo ./nr-cuup -O <path-to>/ci-scripts/conf_files/gnb-cuup.sa.f1.conf --gNBs.[0].plmn_list.[0].mcc 001 --gNBs.[0].plmn_list.[0].mnc 01 --gNBs.[0].local_s_address "127.0.0.6" --gNBs.[0].E1_INTERFACE.[0].ipv4_cucp "127.0.0.3" --gNBs.[0].E1_INTERFACE.[0].ipv4_cuup "127.0.0.6" --gNBs.[0].NETWORK_INTERFACES.GNB_IPV4_ADDRESS_FOR_NG_AMF "192.168.70.129" --gNBs.[0].NETWORK_INTERFACES.GNB_IPV4_ADDRESS_FOR_NGU "192.168.70.129" --e2_agent.near_ric_ip_addr "127.0.0.1" --e2_agent.sm_dir "/usr/local/lib/flexric/" --rfsim
```

---

### 3.4 Start the nrUE

Download the `multi-ue.sh` script and make it executable:

```bash
chmod +x ./multi-ue.sh
```

---

#### 3.4.1 Deploy the First UE

For the first UE, create the namespace `ue1` (`-c1`), then execute shell inside (`-o1`, "open"):

```bash
sudo ./multi-ue.sh -c1
sudo ./multi-ue.sh -o1
```

After entering the bash environment, run the following command to deploy your first UE:

```bash
sudo ./nr-uesoftmodem -O ../../../targets/PROJECTS/GENERIC-NR-5GC/CONF/ue.conf -r 106 --numerology 1 --band 78 -C 3619200000 --rfsim --uicc0.imsi 208930000000001 --rfsimulator.serveraddr 10.201.1.100
```

---

#### 3.4.2 Deploy the Second UE

For the second UE, create the namespace `ue2` (`-c2`), then execute shell inside (`-o2`, "open"):

```bash
sudo ./multi-ue.sh -c2
sudo ./multi-ue.sh -o2
```

After entering the bash environment, run the following command to deploy your second UE:

```bash
sudo ./nr-uesoftmodem -O ../../../targets/PROJECTS/GENERIC-NR-5GC/CONF/ue2.conf -r 106 --numerology 1 --band 78 -C 3619200000 --rfsim --uicc0.imsi 208930000000002 --rfsimulator.serveraddr 10.202.1.100
```

> **Note:**  
> In the command above, please note that the **IMSI** and the **telnet port** have changed.

---

### 3.5 Configure Network Routing for UEs

After running the UEs, enter the following commands in each namespace:

#### For UE1:

```bash
sudo ./multi-ue.sh -o1
sudo ip netns exec ue1 ip route add default dev oaitun_ue1
sudo ip netns exec ue1 ip route
sudo iptables -t nat -A POSTROUTING -s 10.0.0.0/24 -o v-ue1 -j MASQUERADE
```

#### For UE2:

```bash
sudo ./multi-ue.sh -o2
sudo ip netns exec ue2 ip route add default dev oaitun_ue1
sudo ip netns exec ue2 ip route
sudo iptables -t nat -A POSTROUTING -s 10.0.0.0/24 -o v-ue2 -j MASQUERADE
```

> **Note:**  
> Make sure to download the UE-specific configuration files from this repository and place them in the appropriate configuration file directory.

---

## 4. Start the nearRT-RIC and xApps

### 4.1 Start the nearRT-RIC

```bash
cd flexric # or openairinterface5g/openair2/E2AP/flexric
./build/examples/ric/nearRT-RIC
```

> **Note:**  
> The `XAPP_DURATION` environment variable overwrites the default xApp duration of 20 seconds.

---

### 4.2 Start the xApps

**Important:** If no RIC INDICATION is received by any of the xApps, verify your E2 node configuration and connectivity.

#### 4.2.1 Start the KPM Monitor xApp

This xApp provides measurements as stated in **3.1.1 E2SM-KPM** for each UE that matches the S-NSSAI `(1, 0xffffff)` common criteria:

```bash
cd flexric # or openairinterface5g/openair2/E2AP/flexric
XAPP_DURATION=30 ./build/examples/xApp/c/monitor/xapp_kpm_moni
```

---

### 4.3 Generate Traffic

To generate traffic and observe xApp outputs during execution, you can use the `traffic_gen_bursty.sh` script provided in this repository.

```bash
chmod +x ./traffic_gen_bursty.sh
./traffic_gen_bursty.sh
```

This will help you visualize the xApp metrics in real-time while traffic is flowing through the network.

---

## 🧾 License

This repository follows the licensing terms of the original OAI CN5G components.  
For detailed licensing information, please refer to the [OAI GitLab repository](https://gitlab.eurecom.fr/oai/openairinterface5g).

---

© 2025 – 5G-industry4.0-sim Project
