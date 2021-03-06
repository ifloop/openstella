/*
 * I2CController.cpp
 *
 * Copyright 2012 Germaneers GmbH
 * Copyright 2012 Hubert Denkmair (hubert.denkmair@germaneers.com)
 * Copyright 2012 Stefan Rupp (stefan.rupp@germaneers.com)
 *
 * This file is part of libopenstella.
 *
 * libopenstella is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * libopenstella is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libopenstella.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


#include "I2CController.h"

#include <StellarisWare/driverlib/rom.h>
#include <StellarisWare/inc/hw_types.h>
#include <StellarisWare/inc/hw_memmap.h>
#include <StellarisWare/driverlib/sysctl.h>
#include <StellarisWare/driverlib/i2c.h>

void I2C0IntHandler(void) {
	I2CController::_controllers[0]->handleInterrupt();
}

void I2C1IntHandler(void) {
	I2CController::_controllers[1]->handleInterrupt();
}

I2CController::I2CController(controller_t controller, uint32_t periph, uint32_t base)
 : _controller(controller), _periph(periph), _base(base), _sda(GPIO::A[0]), _scl(GPIO::A[0]), _lock()
{
}

void I2CController::handleInterrupt()
{

}

I2CController *I2CController::_controllers[controller_count];
I2CController *I2CController::get(controller_t controller)
{
	static Mutex mutex;
	mutex.take();

	if (!_controllers[controller])
	{
		switch (controller) {
			case controller_0:
				_controllers[controller] = new I2CController(controller, SYSCTL_PERIPH_I2C0, I2C0_MASTER_BASE);
				I2CIntRegister(I2C0_MASTER_BASE, I2C0IntHandler);
				break;
			case controller_1:
				_controllers[controller] = new I2CController(controller, SYSCTL_PERIPH_I2C1, I2C1_MASTER_BASE);
				I2CIntRegister(I2C1_MASTER_BASE, I2C1IntHandler);
				break;
			case controller_count:
				while(1); break;
		}
	}

	mutex.give();

	return _controllers[controller];
}

void I2CController::setup(GPIOPin sda, GPIOPin scl, speed_t speed)
{
	MutexGuard guard(&_lock);
	_sda = sda;
	_scl = scl;

	ROM_SysCtlPeripheralEnable(_periph);
	SysCtlPeripheralReset(_periph);

	_sda.enablePeripheral();
	if (_base == I2C0_MASTER_BASE) {
		_sda.mapAsI2C0SDA();
	}
	else if (_base == I2C1_MASTER_BASE) {
		_sda.mapAsI2C1SDA();
	}
	else {
		while(1) { /* we should never get here! */ }
	}
	_sda.configure(GPIOPin::I2C);

	_scl.enablePeripheral();
	if (_base == I2C0_MASTER_BASE) {
		_scl.mapAsI2C0SCL();
	}
	else if (_base == I2C1_MASTER_BASE) {
		_scl.mapAsI2C1SCL();
	}
	else {
		while(1) { /* we should never get here! */ }
	}
	_scl.configure(GPIOPin::I2CSCL);

	I2CMasterInitExpClk(_base, ROM_SysCtlClockGet(), (speed==speed_400kBit) ? 1 : 0);
	I2CMasterEnable(_base);

    // Do a dummy receive to make sure you don't get junk on the first receive.
    I2CMasterControl(_base, I2C_MASTER_CMD_SINGLE_RECEIVE);
	while(I2CMasterBusy(_base));

}

unsigned long I2CController::waitFinish()
{
	unsigned long ret;

	while (I2CMasterBusy(_base)) {
		ret = I2CMasterErr(_base);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	}
	ret = I2CMasterErr(_base);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	return 0;
}

unsigned long I2CController::nolock_read(uint8_t addr, uint8_t *buf, int count, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;

	if (count<1) return 0;
	ret = nolock_read8(addr, buf, sendStartCondition, (sendStopCondition && count==1));
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}

	for (uint8_t i=1; i<count; i++) {
		I2CMasterControl(_base, ((i==(count-1)) && sendStopCondition) ? I2C_MASTER_CMD_BURST_RECEIVE_FINISH : I2C_MASTER_CMD_BURST_RECEIVE_CONT);
		ret = I2CMasterErr(_base);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = waitFinish();
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		buf[i] = I2CMasterDataGet(_base);
		ret = I2CMasterErr(_base);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}

	}

	return 0;
}

unsigned long I2CController::nolock_read8(uint8_t addr, uint8_t *data, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;

	I2CMasterSlaveAddrSet(_base, addr, 1);
	if (sendStartCondition) {
		I2CMasterControl(_base, sendStopCondition ? I2C_MASTER_CMD_SINGLE_SEND : I2C_MASTER_CMD_BURST_RECEIVE_START);
		ret = I2CMasterErr(_base);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	} else {
		I2CMasterControl(_base, sendStopCondition ? I2C_MASTER_CMD_BURST_RECEIVE_FINISH : I2C_MASTER_CMD_BURST_RECEIVE_CONT);
		ret = I2CMasterErr(_base);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	}
	ret = waitFinish();
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}

	*data = I2CMasterDataGet(_base);
	ret = I2CMasterErr(_base);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	return 0;
}

unsigned long I2CController::nolock_write(uint8_t addr, uint8_t *buf, int count, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;

	if (count<1) return 0;
	nolock_write8(addr, buf[0], sendStartCondition, (count==1) && sendStopCondition);
	ret = I2CMasterErr(_base);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	for(int i=1; i<count; i++) {
		I2CMasterDataPut(_base, buf[i]);
		I2CMasterControl(_base, (sendStopCondition && (i == count-1)) ? I2C_MASTER_CMD_BURST_SEND_FINISH : I2C_MASTER_CMD_BURST_SEND_CONT);
		ret = I2CMasterErr(_base);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = waitFinish();
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	}
  return 0;
}

unsigned long I2CController::nolock_write8(uint8_t addr, uint8_t data, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;

	I2CMasterSlaveAddrSet(_base, addr, 0);
	I2CMasterDataPut(_base, data);
	if (sendStartCondition) {
		I2CMasterControl(_base, sendStopCondition ? I2C_MASTER_CMD_SINGLE_SEND : I2C_MASTER_CMD_BURST_SEND_START);
		ret = I2CMasterErr(_base);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}

	} else {
		I2CMasterControl(_base, sendStopCondition ? I2C_MASTER_CMD_BURST_SEND_FINISH : I2C_MASTER_CMD_BURST_SEND_CONT);
		ret = I2CMasterErr(_base);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}

	}
	ret = waitFinish();
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}

	return 0;
}




unsigned long I2CController::read(uint8_t addr, uint8_t *buf, int count, bool sendStartCondition, bool sendStopCondition)
{
	MutexGuard guard(&_lock);
	return nolock_read(addr, buf, count, sendStartCondition, sendStopCondition);
}

unsigned long I2CController::read8(uint8_t addr, uint8_t *data, bool sendStartCondition, bool sendStopCondition)
{
	MutexGuard guard(&_lock);
	return nolock_read8(addr, data, sendStartCondition, sendStopCondition);
}

unsigned long I2CController::write(uint8_t addr, uint8_t *buf, int count, bool sendStartCondition, bool sendStopCondition)
{
	MutexGuard guard(&_lock);
	return nolock_write(addr, buf, count, sendStartCondition, sendStopCondition);
}

unsigned long I2CController::write8(uint8_t addr, uint8_t data, bool sendStartCondition, bool sendStopCondition)
{
	return nolock_write8(addr, data, sendStartCondition, sendStopCondition);
}

unsigned long I2CController::writeRead(uint8_t addr, uint8_t *writeBuf, uint8_t writeCount, uint8_t *readBuf, uint8_t readCount, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;

	MutexGuard guard(&_lock);
	ret = nolock_write(addr, writeBuf, writeCount, sendStartCondition, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read(addr, readBuf, readCount, true, sendStopCondition);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	return 0;
}

unsigned long I2CController::write8read(uint8_t addr, uint8_t writeData, uint8_t *readBuf, int readCount, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;

	MutexGuard guard(&_lock);
	ret = nolock_write8(addr, writeData, sendStartCondition, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read(addr, readBuf, readCount, true, sendStopCondition);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	return 0;
}

unsigned long I2CController::write8read8(uint8_t addr, uint8_t data_w, uint8_t *data_r, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;

	MutexGuard guard(&_lock);
	ret = nolock_write8(addr, data_w, sendStartCondition, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read8(addr, data_r, true, sendStopCondition);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	return 0;
}

unsigned long I2CController::read16(uint8_t addr, uint16_t *data, byteorder_t byteorder, bool sendStartCondition, bool sendStopCondition)
{
	MutexGuard guard(&_lock);
	uint8_t b1, b2;
	unsigned long ret;

	ret = nolock_read8(addr, &b1, sendStartCondition, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read8(addr, &b2, false, sendStopCondition);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}

	if (byteorder==byteorder_big_endian) {
		*data = (b1<<8) | b2;
	} else {
		*data =  (b2<<8) | b1;
	}
	return 0;
}

unsigned long I2CController::read32(uint8_t addr, uint32_t *data, byteorder_t byteorder, bool sendStartCondition, bool sendStopCondition)
{
	MutexGuard guard(&_lock);
	uint8_t b1, b2, b3, b4;
	unsigned long ret;

	ret = nolock_read8(addr, &b1, sendStartCondition, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read8(addr, &b2, false, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read8(addr, &b3, false, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read8(addr, &b4, false, sendStopCondition);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}

	if (byteorder==byteorder_big_endian) {
		*data = (b1<<24) | (b2<<16) | (b3<<8) | b4;
	} else {
		*data = (b4<<24) | (b3<<16) | (b2<<8) | b1;
	}
	return 0;
}

unsigned long I2CController::write16(uint8_t addr, uint16_t data, byteorder_t byteorder, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;

	MutexGuard guard(&_lock);
	if (byteorder==byteorder_big_endian) {
		ret = nolock_write8(addr, data>>8,   sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data&0xFF, false, sendStopCondition);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	} else {
		ret = nolock_write8(addr, data&0xFF, sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data>>8,   false, sendStopCondition);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	}
	return 0;
}

unsigned long I2CController::write32(uint8_t addr, uint32_t data, byteorder_t byteorder, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;

	MutexGuard guard(&_lock);

	if (byteorder==byteorder_big_endian) {
		ret = nolock_write8(addr, data>>24, sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data>>16, false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data>>8,  false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data,     false, sendStopCondition);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	} else {
		ret = nolock_write8(addr, data,     sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data>>8,  false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data>>16, false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data>>24, false, sendStopCondition);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	}
	return 0;
}

unsigned long I2CController::write8read16(uint8_t addr, uint8_t data_w, uint16_t *data_r, byteorder_t byteorder, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;
	uint8_t b1, b2;

	MutexGuard guard(&_lock);

	ret = nolock_write8(addr, data_w, sendStartCondition, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read8(addr, &b1, true, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read8(addr, &b2, false, sendStopCondition);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}

	if (byteorder==byteorder_big_endian) {
		*data_r = (b1<<8) | b2;
	} else {
		*data_r = (b2<<8) | b1;
	}

	return 0;
}

unsigned long I2CController::write8read32(uint8_t addr, uint8_t data_w, uint32_t *data_r, byteorder_t byteorder, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;
	uint8_t b1, b2, b3, b4;
	MutexGuard guard(&_lock);

	ret = nolock_write8(addr, data_w, sendStartCondition, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read8(addr, &b1, true, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read8(addr, &b2, false, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read8(addr, &b3, false, false);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	ret = nolock_read8(addr, &b4, false, sendStopCondition);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}

	if (byteorder==byteorder_big_endian) {
		*data_r = (b1<<24) | (b2<<16) | (b3<<8) | b4;
	} else {
		*data_r = (b4<<24) | (b3<<16) | (b2<<8) | b1;
	}

	return 0;
}

unsigned long I2CController::write16read(uint8_t addr, uint16_t writeData, uint8_t *readBuf, int readCount, byteorder_t byteorder, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;

	MutexGuard guard(&_lock);
	if (byteorder==byteorder_big_endian) {
		ret = nolock_write8(addr, writeData>>8,   sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, writeData&0xFF, false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	} else {
		ret = nolock_write8(addr, writeData&0xFF, sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, writeData>>8,   false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	}
	ret = nolock_read(addr, readBuf, readCount, true, sendStopCondition);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}

	return 0;
}

unsigned long I2CController::write16read8(uint16_t addr, uint16_t data_w, uint8_t *data_r, byteorder_t byteorder, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;

	MutexGuard guard(&_lock);
	if (byteorder==byteorder_big_endian) {
		ret = nolock_write8(addr, data_w>>8,   sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data_w&0xFF, false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	} else {
		ret = nolock_write8(addr, data_w&0xFF, sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data_w>>8,   false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
	}
	ret = nolock_read8(addr, data_r, true, sendStopCondition);
	if (ret != I2C_MASTER_ERR_NONE) {
		return ret;
	}
	return 0;
}

unsigned long I2CController::write16read16(uint16_t addr, uint16_t data_w, uint16_t *data_r, byteorder_t byteorder, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;
	uint8_t b1, b2;

	MutexGuard guard(&_lock);
	if (byteorder==byteorder_big_endian) {
		ret = nolock_write8(addr, data_w>>8,   sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data_w&0xFF, false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b1, true, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b2, false, sendStopCondition);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		*data_r = (b1<<8) | b2;
	} else {
		ret = nolock_write8(addr, data_w&0xFF, sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data_w>>8,   false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b1, true, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b2, false, sendStopCondition);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		*data_r = (b2<<8) | b1;
	}
	return 0;
}

unsigned long I2CController::write16read32(uint16_t addr, uint16_t data_w, uint32_t *data_r, byteorder_t byteorder, bool sendStartCondition, bool sendStopCondition)
{
	unsigned long ret;
	uint8_t b1, b2, b3, b4;

	MutexGuard guard(&_lock);
	if (byteorder==byteorder_big_endian) {
		ret = nolock_write8(addr, data_w>>8,   sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data_w&0xFF, false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b1, true, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b2, false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b3, false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b4, false, sendStopCondition);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		*data_r = (b1<<24) | (b2<<16) | (b3<<8) | b4;
	} else {
		ret = nolock_write8(addr, data_w&0xFF, sendStartCondition, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_write8(addr, data_w>>8,   false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b1, true, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b2, false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b3, false, false);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		ret = nolock_read8(addr, &b4, false, sendStopCondition);
		if (ret != I2C_MASTER_ERR_NONE) {
			return ret;
		}
		*data_r = (b4<<24) | (b3<<16) | (b2<<8) | b1;
	}
	return 0;

}


