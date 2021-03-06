/***************************** Include Files *********************************/

#include "xparameters.h"
#include "xparameters_ps.h"
#include "xscugic.h"
#include "xil_exception.h"
#include <stdio.h>
#include "xspips.h"
#include "xscutimer.h"
#include "math.h"

/*
 *  This is the size of the buffer to be transmitted/received in this example.
 */
#define BUFFER_SIZE 4


/**************************** Type Definitions *******************************/
/*
 * The following data type is used to send and receive data on the SPI
 * interface.
 */
typedef u8 DataBuffer[BUFFER_SIZE];

/************************** Function Prototypes ******************************/

void SpiIntrHandler(void *CallBackRef, u32 StatusEvent, u32 ByteCount);
void display_buffers(void);
void clear_SPI_buffers(void);
u8 read_current_light_level(XSpiPs *SpiInstance);


/************************** Variable Definitions *****************************/


/*
 * The following variables are shared between non-interrupt processing and
 * interrupt processing such that they must be global.
 */
volatile int SPI_TransferInProgress;
int SPI_Error_Count;


/*
 * The following variables are used to read and write to the Spi device, they
 * are global to avoid having large buffers on the stack.
 */
u8 ReadBuffer[BUFFER_SIZE];
u8 WriteBuffer[BUFFER_SIZE];



int main(void)
{
	XSpiPs_Config *SPI_ConfigPtr;
	XScuGic_Config *IntcConfig;
	XScuGic IntcInstance;		/* Interrupt Controller Instance */
	static XSpiPs SpiInstance;	 /* The instance of the SPI device */

	int Status;
	int timer_value;
	int light_level = 0;
	int previous_light_level = 0;

	int transaction_count = 10;

	// Declare two structs.  One for the Timer instance, and
	// the other for the timer's config information
	XScuTimer my_Timer;
	XScuTimer_Config *Timer_Config;

	// Look up the the config information for the timer
	Timer_Config = XScuTimer_LookupConfig(XPAR_PS7_SCUTIMER_0_DEVICE_ID);

	// Initialise the timer using the config information
	Status = XScuTimer_CfgInitialize(&my_Timer, Timer_Config, Timer_Config->BaseAddr);

	// Load the timer with a value that represents one second
	// The SCU Timer is clocked at half the freq of the CPU.
	XScuTimer_LoadTimer(&my_Timer, XPAR_PS7_CORTEXA9_0_CPU_CLK_FREQ_HZ / 2);

	// Start the timer running (it counts down)
	XScuTimer_Start(&my_Timer);

	// Initialise the SPI driver so that it is ready to use.
	SPI_ConfigPtr = XSpiPs_LookupConfig(XPAR_PS7_SPI_1_DEVICE_ID);
	if (SPI_ConfigPtr == NULL) return XST_DEVICE_NOT_FOUND;
	Status = XSpiPs_CfgInitialize(&SpiInstance, SPI_ConfigPtr, SPI_ConfigPtr->BaseAddress);
	if (Status != XST_SUCCESS) return XST_FAILURE;

	// Perform a self-test to check the SPI hardware
	Status = XSpiPs_SelfTest(&SpiInstance);
	if (Status != XST_SUCCESS) return XST_FAILURE;

	// Reset the SPI peripheral
	XSpiPs_Reset(&SpiInstance);

	// Initialise the Interrupt controller so that it is ready to use.
	IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
	if (NULL == IntcConfig) return XST_FAILURE;
	Status = XScuGic_CfgInitialize(&IntcInstance, IntcConfig, IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) return XST_FAILURE;

	// Initialise exceptions on the ARM processor
	Xil_ExceptionInit();

	// Connect the interrupt controller interrupt handler to the hardware interrupt handling logic in the processor.
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT, (Xil_ExceptionHandler)XScuGic_InterruptHandler, &IntcInstance);


	// Connect a device driver handler that will be called when an interrupt
	// for the device occurs, the device driver handler performs the
	// specific interrupt processing for the device.
	Status = XScuGic_Connect(&IntcInstance, XPS_SPI1_INT_ID, (Xil_ExceptionHandler)XSpiPs_InterruptHandler, (void *)&SpiInstance);
	if (Status != XST_SUCCESS) return Status;

	// Enable the interrupt for the SPI peripheral.
	XScuGic_Enable(&IntcInstance, XPS_SPI1_INT_ID);

	// Enable interrupts in the Processor.
	Xil_ExceptionEnable();


	printf("ADC081S021 PMOD test\n\r\n\r");

	// Setup the handler for the SPI that will be called from the interrupt
	// context when an SPI status occurs, specify a pointer to the SPI
	// driver instance as the callback reference so the handler is able to
	// access the instance data.
	XSpiPs_SetStatusHandler(&SpiInstance, &SpiInstance, (XSpiPs_StatusHandler)SpiIntrHandler);


	// Set the SPI device to the correct mode for this application
	printf("Setting the SPI device into Master mode...");
	// Status = XSpiPs_SetOptions(&SpiInstance, XSPIPS_MASTER_OPTION | XSPIPS_FORCE_SSELECT_OPTION | XSPIPS_CLK_PHASE_1_OPTION);
	// Status = XSpiPs_SetOptions(&SpiInstance, XSPIPS_MASTER_OPTION | XSPIPS_FORCE_SSELECT_OPTION);
	Status = XSpiPs_SetOptions(&SpiInstance, XSPIPS_MASTER_OPTION | XSPIPS_CLK_ACTIVE_LOW_OPTION);
	if (Status != XST_SUCCESS) return XST_FAILURE;
	printf("DONE!!\n\r");

	printf("Setting the SPI device CLK pre-scaler...");
	Status = XSpiPs_SetClkPrescaler(&SpiInstance, XSPIPS_CLK_PRESCALE_64);
	if (Status != XST_SUCCESS) return XST_FAILURE;
	printf("DONE!!\n\r");

	// Select the SPI Slave.  This asserts the correct SS bit on the SPI bus
	XSpiPs_SetSlaveSelect(&SpiInstance, 0x02);


	// An endless loop which reads and displays the current temperature
	while(transaction_count)
	{
		// Read the value of the timer
		timer_value = XScuTimer_GetCounterValue(&my_Timer);

		// If the timer has reached zero
		if (timer_value == 0)
		{
			// Re-load the original value into the timer and re-start it
			XScuTimer_RestartTimer(&my_Timer);

			light_level = read_current_light_level(&SpiInstance);

			// Check to see if the light level is different from the last reading.
			// Only update the display on the UART if it is different.
			//		if (previous_light_level != light_level)
			//		{
			printf("Light Level = %d\n\r", light_level);
			//			previous_light_level = light_level;
			//		}
			transaction_count--;
		}
		else
		{
			// Show the value of the timer's counter value, for debugging purposes
			//printf("Timer is still running (Timer value = %d)\n\r", timer_value);
		}
	}


	// Disable and disconnect the interrupt system.
	XScuGic_Disconnect(&IntcInstance, XPS_SPI1_INT_ID);

	printf("FINISHED!\n\r");

	return XST_SUCCESS;
}



void SpiIntrHandler(void *CallBackRef, u32 StatusEvent, u32 ByteCount)
{
	//printf("** In the SPI Interrupt handler **\n\r");
	//printf("Number of bytes transferred, as seen by the handler = %d\n\r", ByteCount);

	// Indicate the transfer on the SPI bus is no longer in progress
	// regardless of the status event.
	if (StatusEvent == XST_SPI_TRANSFER_DONE)
	{
		SPI_TransferInProgress = FALSE;
	}
	else	// If the event was not transfer done, then track it as an error.
	{
		printf("\n\r\n\r ** SPI ERROR **\n\r\n\r");
		SPI_TransferInProgress = FALSE;
		SPI_Error_Count++;
	}
}


void display_buffers(void)
{
	int i;
	for(i=0; i<BUFFER_SIZE; i++)
	{
		printf("Index 0x%02X  -->  Write = 0x%02X  |  Read = 0x%02X\n\r", i, WriteBuffer[i], ReadBuffer[i]);
	}
}

void clear_SPI_buffers(void)
{
	int SPI_Count;

	// Initialize the write buffer and read buffer to zero
	for (SPI_Count = 0; SPI_Count < BUFFER_SIZE; SPI_Count++)
	{
		WriteBuffer[SPI_Count] = 0;
		ReadBuffer[SPI_Count] = 0;
	}

}

u8 read_current_light_level(XSpiPs *SpiInstance)
{
	u8 Light_Level_Byte_One = 0;
	u8 Light_Level_Byte_Two = 0;
	u16 Light_Level_16bit = 0;
	u8 Light_Level_8bit = 0;
	XStatus Status = 0;

	// Clear the SPI read and write buffers
	clear_SPI_buffers();

	// Put the dummy bytes for the ADC081S021 device in the write buffer
	WriteBuffer[0] = 0x03;
	WriteBuffer[1] = 0x03;

	// Transfer the data.
	SPI_TransferInProgress = TRUE;
	Status = XSpiPs_Transfer(SpiInstance, WriteBuffer, ReadBuffer, 2);
	if (Status != XST_SUCCESS) printf("ALERT! - Disaster and chaos has occurred!!\n\r");

	while (SPI_TransferInProgress);  // Wait here until the SPI transfer has finished

	// Fetch the byte of data from the ReadBuffer, masking out the padding
	// Technically the padding shouldn't matter because it should be zeros, but let's be sure!
	Light_Level_Byte_One = ReadBuffer[0] & 0x0F;
	Light_Level_Byte_Two = ReadBuffer[1] & 0xF0;

	Light_Level_16bit = Light_Level_Byte_One << 8;
	Light_Level_16bit += (Light_Level_Byte_Two >> 4);

	Light_Level_8bit = (u8)Light_Level_16bit;

	return (Light_Level_8bit);
}
