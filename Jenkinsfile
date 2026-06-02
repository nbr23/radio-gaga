pipeline {
	agent any

	options {
		disableConcurrentBuilds()
		ansiColor('xterm')
	}

	environment {
		ARDUINO_CLI_VERSION = '1.5.0'
		ESP32_INDEX = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
		FQBN = 'esp32:esp32:esp32'
		ARDUINO_DIRECTORIES_DATA = "${WORKSPACE}/.arduino15"
		ARDUINO_DIRECTORIES_USER = "${WORKSPACE}/.arduino-user"
	}

	stages {
		stage('Checkout'){
			steps {
				checkout scm
			}
		}
		stage('Build') {
			steps {
				sh '''
					set -eu
					export PATH="$WORKSPACE/bin:$PATH"
					mkdir -p "$WORKSPACE/bin"
					if ! arduino-cli version 2>/dev/null | grep -q "Version: $ARDUINO_CLI_VERSION"; then
						curl -fsSL "https://raw.githubusercontent.com/arduino/arduino-cli/v$ARDUINO_CLI_VERSION/install.sh" | BINDIR="$WORKSPACE/bin" sh -s "$ARDUINO_CLI_VERSION"
					fi

					arduino-cli core update-index --additional-urls "$ESP32_INDEX"
					arduino-cli core install esp32:esp32 --additional-urls "$ESP32_INDEX"

					SKETCH="$WORKSPACE/.sketch/radio-gaga"
					rm -rf "$SKETCH"
					mkdir -p "$SKETCH"
					cp radio-gaga.ino config.example.h "$SKETCH/"
					cp config.example.h "$SKETCH/config.h"

					arduino-cli compile --fqbn "$FQBN" --warnings all "$SKETCH"
				'''
			}
		}
		stage('Sync github repo') {
			when { branch 'master' }
			steps {
					ghSync()
			}
		}
	}
}
