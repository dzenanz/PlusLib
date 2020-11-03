/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include "vtkPlusDataSource.h"
#include "vtkPlusAndorCamera.h"
#include "ATMCD32D.h"
#include "igtlOSUtil.h" // for Sleep
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"


vtkStandardNewMacro(vtkPlusAndorCamera);

// put these here so there is no public dependence on OpenCV
cv::Mat cvCameraIntrinsics;
cv::Mat cvDistanceCoefficients;
cv::Mat cvFlatCorrection;
cv::Mat cvBiasCorrection;

// ----------------------------------------------------------------------------
void vtkPlusAndorCamera::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Shutter: " << Shutter << std::endl;
  os << indent << "ExposureTime: " << ExposureTime << std::endl;
  os << indent << "Binning: " << HorizontalBins << " " << VerticalBins << std::endl;
  os << indent << "HSSpeed: " << HSSpeed[0] << HSSpeed[1] << std::endl;
  os << indent << "VSSpeed: " << VSSpeed << std::endl;
  os << indent << "PreAmpGain: " << PreAmpGain << std::endl;
  os << indent << "AcquisitionMode: " << AcquisitionMode << std::endl;
  os << indent << "ReadMode: " << ReadMode << std::endl;
  os << indent << "TriggerMode: " << TriggerMode << std::endl;
  os << indent << "UseCooling: " << UseCooling << std::endl;
  os << indent << "CoolTemperature: " << CoolTemperature << std::endl;
  os << indent << "SafeTemperature: " << SafeTemperature << std::endl;
  os << indent << "CurrentTemperature: " << CurrentTemperature << std::endl;
  os << indent << "CameraIntrinsics: " << cvCameraIntrinsics << std::endl;
  os << indent << "DistanceCoefficients: " << cvDistanceCoefficients << std::endl;
  os << indent << "FlatCorrection: " << flatCorrection << std::endl;
  os << indent << "BiasCorrection: " << biasCorrection << std::endl;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  LOG_TRACE("vtkPlusAndorCamera::ReadConfiguration");
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);

  // Must initialize the system before setting parameters
  checkStatus(Initialize(""), "Initialize");

  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, Shutter, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(float, ExposureTime, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, PreAmpGain, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, AcquisitionMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, ReadMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, TriggerMode, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, CoolTemperature, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, SafeTemperature, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, VSSpeed, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, HorizontalBins, deviceConfig);
  XML_READ_SCALAR_ATTRIBUTE_OPTIONAL(int, VerticalBins, deviceConfig);

  XML_READ_BOOL_ATTRIBUTE_OPTIONAL(UseCooling, deviceConfig);

  deviceConfig->GetVectorAttribute("HSSpeed", 2, HSSpeed);
  deviceConfig->GetVectorAttribute("CameraIntrinsics", 9, cameraIntrinsics);
  deviceConfig->GetVectorAttribute("DistanceCoefficients", 4, distanceCoefficients);
  flatCorrection = deviceConfig->GetAttribute("FlatCorrection");
  biasCorrection = deviceConfig->GetAttribute("BiasCorrection");

  cvCameraIntrinsics = cv::Mat(3, 3, CV_64FC1, cameraIntrinsics);
  cvDistanceCoefficients = cv::Mat(1, 4, CV_64FC1, distanceCoefficients);
  try
  {
    cvFlatCorrection = cv::imread(flatCorrection, cv::IMREAD_GRAYSCALE);
    double maxVal = 0.0;
    cv::minMaxLoc(cvFlatCorrection, nullptr, &maxVal);
    if(maxVal > 1.0)  // we need to normalize the image to [0.0, 1.0] range
    {
      cv::Mat temp;
      cvFlatCorrection.convertTo(temp, CV_32FC1, 1.0 / maxVal);
      cvFlatCorrection = temp;
    }
  }
  catch(...)
  {
    LOG_ERROR("Could not load flat correction image from file: " << flatCorrection);
    return PLUS_FAIL;
  }
  try
  {
    cvBiasCorrection = cv::imread(biasCorrection, cv::IMREAD_GRAYSCALE);
  }
  catch(...)
  {
    LOG_ERROR("Could not load flat correction image from file: " << biasCorrection);
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(deviceConfig, rootConfigElement);

  deviceConfig->SetIntAttribute("Shutter", this->Shutter);
  deviceConfig->SetFloatAttribute("ExposureTime", this->ExposureTime);
  deviceConfig->SetIntAttribute("PreAmpGain", this->PreAmpGain);
  deviceConfig->SetIntAttribute("AcquisitionMode", this->AcquisitionMode);
  deviceConfig->SetIntAttribute("ReadMode", this->ReadMode);
  deviceConfig->SetIntAttribute("TriggerMode", this->TriggerMode);
  deviceConfig->SetIntAttribute("CoolTemperature", this->CoolTemperature);
  deviceConfig->SetIntAttribute("SafeTemperature", this->SafeTemperature);
  deviceConfig->SetIntAttribute("VSSpeed", this->VSSpeed);
  deviceConfig->SetIntAttribute("HorizontalBins", this->HorizontalBins);
  deviceConfig->SetIntAttribute("VerticalBins", this->VerticalBins);

  deviceConfig->SetVectorAttribute("HSSpeed", 2, HSSpeed);
  deviceConfig->SetVectorAttribute("CameraIntrinsics", 9, cameraIntrinsics);
  deviceConfig->SetVectorAttribute("DistanceCoefficients", 4, distanceCoefficients);
  deviceConfig->SetAttribute("FlatCorrection", flatCorrection.c_str());
  deviceConfig->SetAttribute("BiasCorrection", biasCorrection.c_str());

  XML_WRITE_BOOL_ATTRIBUTE(UseCooling, deviceConfig);

  return PLUS_SUCCESS;
}


// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::NotifyConfigured()
{
  if(this->OutputChannels.empty())
  {
    LOG_ERROR("No output channels defined for vtkPlusAndorCamera. Cannot proceed.");
    this->CorrectlyConfigured = false;
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
std::string vtkPlusAndorCamera::GetSdkVersion()
{
  std::ostringstream versionString;

  char SDKVersion[256];
  checkStatus(GetVersionInfo(AT_SDKVersion, SDKVersion, sizeof(SDKVersion)), "GetVersionInfo");
  versionString << "Andor SDK version: "  << SDKVersion << std::ends;

  return versionString.str();
}


//----------------------------------------------------------------------------
vtkPlusAndorCamera::vtkPlusAndorCamera()
{
  this->RequirePortNameInDeviceSetConfiguration = true;

  this->StartThreadForInternalUpdates = false; // frames should not be acquired automatically
  this->AcquisitionRate = 1.0; // this controls the frequency
}

// ----------------------------------------------------------------------------
vtkPlusAndorCamera::~vtkPlusAndorCamera()
{
  if(!this->Connected)
  {
    this->Disconnect();
  }
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InitializeAndorCamera()
{
  checkStatus(Initialize(""), "Initialize");

  // Check the safe temperature, and the maximum allowable temperature on the camera.
  // Use the min of the two as the safe temp.
  int MinTemp, MaxTemp;
  unsigned result = checkStatus(GetTemperatureRange(&MinTemp, &MaxTemp), "GetTemperatureRange");
  if(result == DRV_SUCCESS)
  {
    LOG_INFO("The temperature range for the connected Andor Camera is: " << MinTemp << " and " << MaxTemp);
  }

  if(MaxTemp < this->SafeTemperature)
  {
    this->SafeTemperature = MaxTemp;
  }
  if(this->CoolTemperature < MinTemp || this->CoolTemperature > MaxTemp)
  {
    LOG_ERROR("Requested temperature for Andor camera is out of range");
    return PLUS_FAIL;
  }

  if(this->UseCooling)
  {
    result = checkStatus(CoolerON(), "CoolerON");
    if(result == DRV_SUCCESS)
    {
      LOG_INFO("Temperature controller switched ON.");
    }
    checkStatus(SetTemperature(this->CoolTemperature), "SetTemperature");
  }
  GetCurrentTemperature(); // logs the status and temperature

  int x, y;
  checkStatus(GetDetector(&x, &y), "GetDetector");
  frameSize[0] = static_cast<unsigned>(x);
  frameSize[1] = static_cast<unsigned>(y);

  // init to binning of 1 (meaning no binning), and full sensor size
  checkStatus(SetImage(this->HorizontalBins, this->VerticalBins, 1, x, 1, y), "SetImage");

  checkStatus(PrepareAcquisition(), "PrepareAcquisition");

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
void vtkPlusAndorCamera::InitializePort(DataSourceArray& port)
{
  for(unsigned i = 0; i < port.size(); i++)
  {
    port[i]->SetPixelType(VTK_UNSIGNED_SHORT);
    port[i]->SetImageType(US_IMG_BRIGHTNESS);
    port[i]->SetOutputImageOrientation(US_IMG_ORIENT_MF);
    port[i]->SetInputImageOrientation(US_IMG_ORIENT_MF);
    port[i]->SetInputFrameSize(frameSize);

    LOG_INFO("Andor source initialized. ID: " << port[i]->GetId());
  }
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalConnect()
{
  LOG_TRACE("vtkPlusAndorCamera::InternalConnect");
  if(this->InitializeAndorCamera() != PLUS_SUCCESS)
  {
    return PLUS_FAIL;
  }

  this->GetVideoSourcesByPortName("BLIraw", BLIraw);
  this->GetVideoSourcesByPortName("BLIrectified", BLIrectified);
  this->GetVideoSourcesByPortName("GrayRaw", GrayRaw);
  this->GetVideoSourcesByPortName("GrayRectified", GrayRectified);

  if(BLIraw.size() + BLIrectified.size() + BLIdark.size() + GrayRaw.size() + GrayRectified.size() + GrayDark.size() == 0)
  {
    vtkPlusDataSource* aSource = nullptr;
    if(this->GetFirstActiveOutputVideoSource(aSource) != PLUS_SUCCESS || aSource == nullptr)
    {
      LOG_ERROR("Standard data sources are not defined, and unable to retrieve the video source in the capturing device.");
      return PLUS_FAIL;
    }
    BLIraw.push_back(aSource); // this is the default port
  }

  this->InitializePort(BLIraw);
  this->InitializePort(BLIrectified);
  this->InitializePort(GrayRaw);
  this->InitializePort(GrayRectified);

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalDisconnect()
{
  LOG_DEBUG("Disconnecting from Andor");
  if(IsRecording())
  {
    this->InternalStopRecording();
  }

  int status;
  checkStatus(IsCoolerOn(&status), "IsCoolerOn");

  if(status)
  {
    GetCurrentTemperature(); // updates this->CurrentTemperature
    if(this->CurrentTemperature < this->SafeTemperature)
    {
      LOG_INFO("Temperature not yet at a safe point, turning the Cooler Off");
      checkStatus(CoolerOFF(), "CoolerOff");

      while(this->CurrentTemperature < this->SafeTemperature)
      {
        igtl::Sleep(5000); // wait a bit
        GetCurrentTemperature(); // logs the status and temperature
      }
    }
  }

  checkStatus(FreeInternalMemory(), "FreeInternalMemory");

  unsigned result = checkStatus(ShutDown(), "ShutDown");
  if(result == DRV_SUCCESS)
  {
    LOG_INFO("Andor camera shut down successfully.");
  }

  return PLUS_SUCCESS;
}


// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalStartRecording()
{
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::InternalStopRecording()
{
  return PLUS_SUCCESS;
}


// ----------------------------------------------------------------------------
float vtkPlusAndorCamera::GetCurrentTemperature()
{
  checkStatus(GetTemperatureF(&this->CurrentTemperature), "GetTemperatureF");
  return this->CurrentTemperature;
}

// ----------------------------------------------------------------------------
void vtkPlusAndorCamera::WaitForCooldown()
{
  if(this->UseCooling == false)
  {
    return;
  }
  while(checkStatus(GetTemperatureF(&this->CurrentTemperature), "GetTemperatureF") != DRV_TEMPERATURE_STABILIZED)
  {
    igtl::Sleep(5000); // wait a bit
  }
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::AcquireFrame(float exposure, int shutterMode, int binning, int vsSpeed, int hsSpeed)
{
  unsigned rawFrameSize = frameSize[0] * frameSize[1];
  rawFrame.resize(rawFrameSize, 0);

  checkStatus(::SetExposureTime(exposure), "SetExposureTime");
  checkStatus(::SetShutter(1, shutterMode, 0, 0), "SetShutter");

  int hbin = binning > 0 ? binning : this->HorizontalBins;
  int vbin = binning > 0 ? binning : this->VerticalBins;
  checkStatus(::SetImage(hbin, vbin, 1, 1024, 1, 1024), "Binning");

  int vsInd = vsSpeed >= 0 ? vsSpeed : this->VSSpeed;
  checkStatus(::SetVSSpeed(vsSpeed), "SetVSSpeed");

  int hsInd = hsSpeed >= 0 ? hsSpeed : this->HSSpeed[1];
  checkStatus(::SetHSSpeed(this->HSSpeed[0], hsSpeed), "SetHSSpeed");
  
  checkStatus(StartAcquisition(), "StartAcquisition");
  unsigned result = checkStatus(WaitForAcquisition(), "WaitForAcquisition");
  if(result == DRV_NO_NEW_DATA)   // Log a more specific log message for WaitForAcquisition
  {
    LOG_ERROR("Non-Acquisition Event occurred.(e.g. CancelWait() called)");
  }
  this->currentTime = vtkIGSIOAccurateTimer::GetSystemTime();

  // iKon-M 934 has 16-bit digitization
  // https://andor.oxinst.com/assets/uploads/products/andor/documents/andor-ikon-m-934-specifications.pdf
  // so we choose 16-bit unsigned
  // GetMostRecentImage() is 32 bit signed variant
  checkStatus(GetMostRecentImage16(&rawFrame[0], rawFrameSize), "GetMostRecentImage16");

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
void vtkPlusAndorCamera::AddFrameToDataSource(DataSourceArray& ds)
{
  for(unsigned i = 0; i < ds.size(); i++)
  {
    if(ds[i]->AddItem(&rawFrame[0],
                      US_IMG_ORIENT_MF,
                      frameSize, VTK_UNSIGNED_SHORT,
                      1, US_IMG_BRIGHTNESS, 0,
                      this->FrameNumber,
                      currentTime,
                      UNDEFINED_TIMESTAMP,
                      nullptr) != PLUS_SUCCESS)
    {
      LOG_WARNING("Error adding item to AndorCamera video source " << ds[i]->GetSourceId());
    }
    else
    {
      LOG_INFO("Success adding item to AndorCamera video source " << ds[i]->GetSourceId());
    }
  }
}


// ----------------------------------------------------------------------------
void vtkPlusAndorCamera::ApplyFrameCorrections()
{
  cv::Mat cvIMG(frameSize[0], frameSize[1], CV_16UC1, &rawFrame[0]); // uses rawFrame as buffer
  cv::Mat floatImage;
  cvIMG.convertTo(floatImage, CV_32FC1);
  cv::Mat result;

  cv::subtract(floatImage, cvBiasCorrection, floatImage, cv::noArray(), CV_32FC1);
  LOG_INFO("Applied constant bias correction");

  // Divide the image by the 32-bit floating point correction image
  cv::divide(floatImage, cvFlatCorrection, floatImage, 1, CV_32FC1);
  LOG_INFO("Applied multiplicative flat correction");

  // OpenCV's lens distortion correction
  cv::undistort(floatImage, result, cvCameraIntrinsics, cvDistanceCoefficients);
  LOG_INFO("Applied lens distortion correction");
  result.convertTo(cvIMG, CV_16UC1);
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::AcquireBLIFrame(int binning, int vsSpeed, int hsSpeed)
{
  WaitForCooldown();
  AcquireFrame(this->ExposureTime, 0, binning, vsSpeed, hsSpeed);
  ++this->FrameNumber;
  AddFrameToDataSource(BLIraw);

  ApplyFrameCorrections();
  AddFrameToDataSource(BLIrectified);

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::AcquireGrayscaleFrame(int binning, int vsSpeed, int hsSpeed, float exposureTime)
{
  WaitForCooldown();
  AcquireFrame(exposureTime, 0, binning, vsSpeed, hsSpeed);
  ++this->FrameNumber;
  AddFrameToDataSource(GrayRaw);

  ApplyFrameCorrections();
  AddFrameToDataSource(GrayRectified);

  return PLUS_SUCCESS;
}


// Setup the Andor camera parameters ----------------------------------------------

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetShutter(int shutter)
{
  this->Shutter = shutter;
  checkStatus(::SetShutter(1, this->Shutter, 0, 0), "SetShutter");
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetShutter()
{
  return this->Shutter;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetExposureTime(float exposureTime)
{
  this->ExposureTime = exposureTime;
  checkStatus(::SetExposureTime(this->ExposureTime), "SetExposureTime");
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
float vtkPlusAndorCamera::GetExposureTime()
{
  return this->ExposureTime;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetHorizontalBins(int bins)
{
  int x, y;
  checkStatus(GetDetector(&x, &y), "GetDetector");  // full sensor size
  unsigned status = checkStatus(::SetImage(bins, this->VerticalBins, 1, x, 1, y), "SetImage");
  if(status != DRV_SUCCESS)
  {
    return PLUS_FAIL;
  }
  this->HorizontalBins = bins;
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetVerticalBins(int bins)
{
  int x, y;
  checkStatus(GetDetector(&x, &y), "GetDetector");  // full sensor size
  unsigned status = checkStatus(::SetImage(this->HorizontalBins, bins, 1, x, 1, y), "SetImage");
  if(status != DRV_SUCCESS)
  {
    return PLUS_FAIL;
  }
  this->VerticalBins = bins;
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetHSSpeed(int type, int index)
{
  unsigned status = checkStatus(::SetHSSpeed(type, index), "SetHSSpeed");
  if(status != DRV_SUCCESS)
  {
    return PLUS_FAIL;
  }
  HSSpeed[0] = type;
  HSSpeed[1] = index;
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetVSSpeed(int index)
{
  unsigned status = checkStatus(::SetVSSpeed(index), "SetVSSpeed");
  if(status != DRV_SUCCESS)
  {
    return PLUS_FAIL;
  }
  this->VSSpeed = index;
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetPreAmpGain(int preAmpGain)
{
  this->PreAmpGain = preAmpGain;
  unsigned status = checkStatus(::SetPreAmpGain(this->PreAmpGain), "SetPreAmpGain");
  if(status == DRV_P1INVALID)
  {
    LOG_ERROR("Minimum threshold outside valid range (1-65535).");
  }
  else if(status == DRV_P2INVALID)
  {
    LOG_ERROR("Maximum threshold outside valid range.");
  }
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetPreAmpGain()
{
  return this->PreAmpGain;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetAcquisitionMode(int acquisitionMode)
{
  this->AcquisitionMode = acquisitionMode;
  checkStatus(::SetAcquisitionMode(this->AcquisitionMode), "SetAcquisitionMode");
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetAcquisitionMode()
{
  return this->AcquisitionMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetReadMode(int readMode)
{
  this->ReadMode = readMode;
  checkStatus(::SetReadMode(this->ReadMode), "SetReadMode");
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetReadMode()
{
  return this->ReadMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetTriggerMode(int triggerMode)
{
  this->TriggerMode = triggerMode;
  checkStatus(::SetTriggerMode(this->TriggerMode), "SetTriggerMode");
  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetTriggerMode()
{
  return this->TriggerMode;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetUseCooling(bool useCooling)
{
  int coolerStatus = 1;
  unsigned result = checkStatus(::IsCoolerOn(&coolerStatus), "IsCoolerOn");
  this->UseCooling = useCooling;
  if(useCooling && coolerStatus == 0)
  {
    // Turn the cooler on if we are using cooling
    result = checkStatus(CoolerON(), "CoolerON");
    if(result == DRV_SUCCESS)
    {
      LOG_INFO("Temperature controller switched ON.");
    }
    checkStatus(SetTemperature(this->CoolTemperature), "SetTemperature");
  }
  else if(useCooling == false && coolerStatus == 1)
  {
    // Make sure that if the cooler is on, we wait for warmup
    result = checkStatus(CoolerOFF(), "CoolerOFF");
    if(result == DRV_SUCCESS)
    {
      LOG_INFO("Temperature controller switched OFF.");
    }

    while(this->CurrentTemperature < this->SafeTemperature)
    {
      igtl::Sleep(5000); // wait a bit
      GetCurrentTemperature(); // logs the status and temperature
    }
  }

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
bool vtkPlusAndorCamera::GetUseCooling()
{
  return this->UseCooling;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetCoolTemperature(int coolTemp)
{
  this->CoolTemperature = coolTemp;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetCoolTemperature()
{
  return this->CoolTemperature;
}

// ----------------------------------------------------------------------------
PlusStatus vtkPlusAndorCamera::SetSafeTemperature(int safeTemp)
{
  this->SafeTemperature = safeTemp;

  return PLUS_SUCCESS;
}

// ----------------------------------------------------------------------------
int vtkPlusAndorCamera::GetSafeTemperature()
{
  return this->SafeTemperature;
}

// ----------------------------------------------------------------------------
unsigned int vtkPlusAndorCamera::checkStatus(unsigned int returnStatus, std::string functionName)
{
  if(returnStatus == DRV_SUCCESS)
  {
    return returnStatus;
  }
  else if(returnStatus == DRV_NOT_INITIALIZED)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Driver is not initialized.");
  }
  else if(returnStatus == DRV_ACQUIRING)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Not allowed. Currently acquiring data.");
  }
  else if(returnStatus == DRV_P1INVALID)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Parameter 1 not valid.");
  }
  else if(returnStatus == DRV_P2INVALID)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Parameter 2 not valid.");
  }
  else if(returnStatus == DRV_P3INVALID)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Parameter 3 not valid.");
  }
  else if(returnStatus == DRV_P4INVALID)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Parameter 4 not valid.");
  }
  else if(returnStatus == DRV_P5INVALID)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Parameter 5 not valid.");
  }
  else if(returnStatus == DRV_P6INVALID)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Parameter 6 not valid.");
  }
  else if(returnStatus == DRV_P7INVALID)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Parameter 7 not valid.");
  }
  else if(returnStatus == DRV_ERROR_ACK)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Unable to communicate with card.");
  }
  else if(returnStatus == DRV_TEMP_OFF)
  {
    LOG_INFO("Cooler is OFF. Current temperature is " << this->CurrentTemperature << " °C");
  }
  else if(returnStatus == DRV_TEMPERATURE_STABILIZED)
  {
    LOG_INFO("Temperature has stabilized at " << this->CurrentTemperature << " °C");
  }
  else if(returnStatus == DRV_TEMPERATURE_NOT_REACHED)
  {
    LOG_INFO("Cooling down, current temperature is " << this->CurrentTemperature << " °C");
  }
  else if(returnStatus == DRV_TEMP_DRIFT)
  {
    LOG_INFO("Temperature had stabilised but has since drifted. Current temperature is " << this->CurrentTemperature << " °C");
  }
  else if(returnStatus == DRV_TEMP_NOT_STABILIZED)
  {
    LOG_INFO("Temperature reached but not stabilized. Current temperature is " << this->CurrentTemperature << " °C");
  }
  else if(returnStatus == DRV_VXDNOTINSTALLED)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; VxD not loaded.");
  }
  else if(returnStatus == DRV_INIERROR)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Unable to load DETECTOR.INI.");
  }
  else if(returnStatus == DRV_COFERROR)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Unable to load *.COF.");
  }
  else if(returnStatus == DRV_FLEXERROR)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Unable to load *.RBF.");
  }
  else if(returnStatus == DRV_ERROR_FILELOAD)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Unable to load *.COF or *.RBF files.");
  }
  else if(returnStatus == DRV_USBERROR)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Unable to detect USB device or not USB 2.0.");
  }
  else if(returnStatus == DRV_ERROR_NOCAMERA)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; No camera found.");
  }
  else if(returnStatus == DRV_GENERAL_ERRORS)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; An error occured while obtaining the number of available cameras.");
  }
  else if(returnStatus == DRV_INVALID_MODE)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Invalid mode or mode not available.");
  }
  else if(returnStatus == DRV_ERROR_PAGELOCK)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Unable to allocate memory.");
  }
  else if(returnStatus == DRV_INVALID_FILTER)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Filter not available for current acquisition.");
  }
  else if(returnStatus == DRV_BINNING_ERROR)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Range not a multiple of horizontal binning.");
  }
  else if(returnStatus == DRV_SPOOLSETUPERROR)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Error with spool settings.");
  }
  else if(returnStatus == DRV_IDLE)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; The system is not currently acquiring.");
  }
  else if(returnStatus == DRV_NO_NEW_DATA)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; There is no new data yet.");
  }
  else if(returnStatus == DRV_ERROR_CODES)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Problem communicating with camera.");
  }
  else if(returnStatus == DRV_LOAD_FIRMWARE_ERROR)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Error loading firmware.");
  }
  else if(returnStatus == DRV_NOT_SUPPORTED)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Feature not supported.");
  }
  else if(returnStatus == DRV_RANDOM_TRACK_ERROR)
  {
    LOG_ERROR("Failed AndorSDK operation: " << functionName            \
              << "; Invalid combination of tracks.");
  }
  else
  {
    LOG_WARNING("Possible failed AndorSDK operation: " << functionName            \
                << "; Unknown return code " << returnStatus << "returned.");
  }

  return returnStatus;
}
