.PHONY: *

TARGET ?= esp32s3

ap_fullclean:
	sudo docker run --rm -v ${PWD}:/esp_csi -w /esp_csi/ap -e HOME=/tmp espressif/idf:v5.0.1 idf.py fullclean

ap_build:
	sudo docker run --rm -v ${PWD}:/esp_csi -w /esp_csi/ap -e HOME=/tmp espressif/idf:v5.0.1 sh -c "idf.py set-target ${TARGET} && idf.py build"

ap_flash_monitor:
	sudo docker run --rm -it -v ${PWD}:/esp_csi -w /esp_csi/ap -e HOME=/tmp --device=${PORT} espressif/idf:v5.0.1 sh -c "idf.py -p ${PORT} flash monitor"

ap_monitor:
	sudo docker run --rm -it -v ${PWD}:/esp_csi -w /esp_csi/ap -e HOME=/tmp --device=${PORT} espressif/idf:v5.0.1 sh -c "idf.py -p ${PORT} monitor"

ap_menuconfig:
	sudo docker run --rm -it -v ${PWD}:/esp_csi -w /esp_csi/ap -e HOME=/tmp espressif/idf:v5.0.1 idf.py menuconfig

station_fullclean:
	sudo docker run --rm -v ${PWD}:/esp_csi -w /esp_csi/station -e HOME=/tmp espressif/idf:v5.0.1 idf.py fullclean

station_build:
	sudo docker run --rm -v ${PWD}:/esp_csi -w /esp_csi/station -e HOME=/tmp espressif/idf:v5.0.1 sh -c "idf.py set-target ${TARGET} && idf.py build"

station_flash_monitor:
	sudo docker run --rm -it -v ${PWD}:/esp_csi -w /esp_csi/station -e HOME=/tmp --device=${PORT} espressif/idf:v5.0.1 sh -c "idf.py -p ${PORT} flash monitor"

station_monitor:
	sudo docker run --rm -it -v ${PWD}:/esp_csi -w /esp_csi/station -e HOME=/tmp --device=${PORT} espressif/idf:v5.0.1 sh -c "idf.py -p ${PORT} monitor"

station_menuconfig:
	sudo docker run --rm -it -v ${PWD}:/esp_csi -w /esp_csi/station -e HOME=/tmp espressif/idf:v5.0.1 idf.py menuconfig