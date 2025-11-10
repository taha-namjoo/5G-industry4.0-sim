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

## 🧾 License

This repository follows the licensing terms of the original OAI CN5G components.  
For detailed licensing information, please refer to the [OAI GitLab repository](https://gitlab.eurecom.fr/oai/openairinterface5g).

---

© 2025 – 5G-industry4.0-sim Project
