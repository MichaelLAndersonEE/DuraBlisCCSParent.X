
#define NUM_CAPKEYS  4
    const byte keypadADCchan[NUM_CAPKEYS] = { 20, 21, 22, 23 };
    unsigned long ADC_Sum;  // For averaging muliple ADC msmts
    unsigned iAvg;          // Averaging index
    unsigned Naverages = 32;    // Num avgs, < 2^22
    unsigned Log2Naverages = 5; // Right shift equal to 1 / Naverages
    int iButton;            // Button index
    int iChan;              // ADC chan idx
    int CurrentButtonStatus;    // Bit field of buttons pressed
    unsigned VmeasADC, VavgADC; // Meas voltages, 65535 full scale
    unsigned ButtonVmeasADC[NUM_CAPKEYS]; // To report all voltages at once.

    CTMUCONbits.IRNG = 0x03;    // Current range
    CTMUCONbits.ON = 1;
    delay_us(100);

   while(1)
    {
        CurrentButtonStatus = 0;
        for (iButton = 0; iButton < NUM_CAPKEYS; iButton++)
        {
            iChan = keypadADCchan[iButton];
            AD1CHSbits.CH0SA = iChan;
            ADC_Sum = 0;
                //iButton = 2;        // TODO 2 only for example
            AD1CON1bits.SAMP = 1; // Manual sampling start
            CTMUCONbits.IDISSEN = 1; // Ground charge pump
            delay_us(1000); // Wait 1 ms for grounding
            CTMUCONbits.IDISSEN = 0; // End drain of circuit

                // switch (iButton)    // WTF??
            CTMUCONbits.EDG1STAT = 1; // Begin charging the circuit
                // TO DO: Wait 33 NOPs for Button 2 charge
            AD1CON1bits.SAMP = 0; // Begin analog-to-digital conversion
            CTMUCONbits.EDG1STAT = 0; // Stop charging circuit
            while (!AD1CON1bits.DONE); // Wait for ADC conversion

            AD1CON1bits.DONE = 0; // ADC conversion done, clear flag
            VmeasADC = ADC1BUF0; // Get the value from the ADC
            ADC_Sum += VmeasADC; // Update averaging sum
        }// end for

        if (Log2Naverages - 6 > 0)
        {
            VavgADC = ADC_Sum >> (Log2Naverages - 6); // Full scale = 2^10<<6 = 65536
        }
        else
        {
            VavgADC = ADC_Sum << (6-Log2Naverages); // Full scale = 2^10<<6 = 65536
        }
        if (VavgADC < 32768) // Button is being pressed
        {
            CurrentButtonStatus += 1<<iButton;
        }
        ButtonVmeasADC[iButton] = VavgADC;
   }