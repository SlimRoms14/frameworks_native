/*
 * Copyright 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <jpegrecoverymap/recoverymaputils.h>
#include <image_io/xml/xml_reader.h>
#include <image_io/xml/xml_writer.h>
#include <image_io/base/message_handler.h>
#include <image_io/xml/xml_element_rules.h>
#include <image_io/xml/xml_handler.h>
#include <image_io/xml/xml_rule.h>

#include <utils/Log.h>

using namespace photos_editing_formats::image_io;
using namespace std;

namespace android::recoverymap {

/*
 * Helper function used for generating XMP metadata.
 *
 * @param prefix The prefix part of the name.
 * @param suffix The suffix part of the name.
 * @return A name of the form "prefix:suffix".
 */
string Name(const string &prefix, const string &suffix) {
  std::stringstream ss;
  ss << prefix << ":" << suffix;
  return ss.str();
}

/*
 * Helper function used for writing data to destination.
 */
status_t Write(jr_compressed_ptr destination, const void* source, size_t length, int &position) {
  if (position + length > destination->maxLength) {
    return ERROR_JPEGR_BUFFER_TOO_SMALL;
  }

  memcpy((uint8_t*)destination->data + sizeof(uint8_t) * position, source, length);
  position += length;
  return NO_ERROR;
}

status_t Write(jr_exif_ptr destination, const void* source, size_t length, int &position) {
  memcpy((uint8_t*)destination->data + sizeof(uint8_t) * position, source, length);
  position += length;
  return NO_ERROR;
}

// Extremely simple XML Handler - just searches for interesting elements
class XMPXmlHandler : public XmlHandler {
public:

    XMPXmlHandler() : XmlHandler() {
        gContainerItemState = NotStrarted;
    }

    enum ParseState {
        NotStrarted,
        Started,
        Done
    };

    virtual DataMatchResult StartElement(const XmlTokenContext& context) {
        string val;
        if (context.BuildTokenValue(&val)) {
            if (!val.compare(gContainerItemName)) {
                gContainerItemState = Started;
            } else {
                if (gContainerItemState != Done) {
                    gContainerItemState = NotStrarted;
                }
            }
        }
        return context.GetResult();
    }

    virtual DataMatchResult FinishElement(const XmlTokenContext& context) {
        if (gContainerItemState == Started) {
            gContainerItemState = Done;
            lastAttributeName = "";
        }
        return context.GetResult();
    }

    virtual DataMatchResult AttributeName(const XmlTokenContext& context) {
        string val;
        if (gContainerItemState == Started) {
            if (context.BuildTokenValue(&val)) {
                if (!val.compare(rangeScalingFactorAttrName)) {
                    lastAttributeName = rangeScalingFactorAttrName;
                } else if (!val.compare(transferFunctionAttrName)) {
                    lastAttributeName = transferFunctionAttrName;
                } else {
                    lastAttributeName = "";
                }
            }
        }
        return context.GetResult();
    }

    virtual DataMatchResult AttributeValue(const XmlTokenContext& context) {
        string val;
        if (gContainerItemState == Started) {
            if (context.BuildTokenValue(&val, true)) {
                if (!lastAttributeName.compare(rangeScalingFactorAttrName)) {
                    rangeScalingFactorStr = val;
                } else if (!lastAttributeName.compare(transferFunctionAttrName)) {
                    transferFunctionStr = val;
                }
            }
        }
        return context.GetResult();
    }

    bool getRangeScalingFactor(float* scaling_factor) {
        if (gContainerItemState == Done) {
            stringstream ss(rangeScalingFactorStr);
            float val;
            if (ss >> val) {
                *scaling_factor = val;
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    bool getTransferFunction(jpegr_transfer_function* transfer_function) {
        if (gContainerItemState == Done) {
            stringstream ss(transferFunctionStr);
            int val;
            if (ss >> val) {
                *transfer_function = static_cast<jpegr_transfer_function>(val);
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
        return true;
    }

private:
    static const string gContainerItemName;
    static const string rangeScalingFactorAttrName;
    static const string transferFunctionAttrName;
    string              rangeScalingFactorStr;
    string              transferFunctionStr;
    string              lastAttributeName;
    ParseState          gContainerItemState;
};

// GContainer XMP constants - URI and namespace prefix
const string kContainerUri        = "http://ns.google.com/photos/1.0/container/";
const string kContainerPrefix     = "GContainer";

// GContainer XMP constants - element and attribute names
const string kConDirectory            = Name(kContainerPrefix, "Directory");
const string kConItem                 = Name(kContainerPrefix, "Item");
const string kConItemLength           = Name(kContainerPrefix, "ItemLength");
const string kConItemMime             = Name(kContainerPrefix, "ItemMime");
const string kConItemSemantic         = Name(kContainerPrefix, "ItemSemantic");
const string kConVersion              = Name(kContainerPrefix, "Version");

// GContainer XMP constants - element and attribute values
const string kSemanticPrimary     = "Primary";
const string kSemanticRecoveryMap = "RecoveryMap";
const string kMimeImageJpeg       = "image/jpeg";

const int kGContainerVersion      = 1;

// GContainer XMP constants - names for XMP handlers
const string XMPXmlHandler::gContainerItemName = kConItem;

// RecoveryMap XMP constants - URI and namespace prefix
const string kRecoveryMapUri      = "http://ns.google.com/photos/1.0/recoverymap/";
const string kRecoveryMapPrefix   = "RecoveryMap";

// RecoveryMap XMP constants - element and attribute names
const string kMapRangeScalingFactor = Name(kRecoveryMapPrefix, "RangeScalingFactor");
const string kMapTransferFunction   = Name(kRecoveryMapPrefix, "TransferFunction");
const string kMapVersion            = Name(kRecoveryMapPrefix, "Version");

const string kMapHdr10Metadata      = Name(kRecoveryMapPrefix, "HDR10Metadata");
const string kMapHdr10MaxFall       = Name(kRecoveryMapPrefix, "HDR10MaxFALL");
const string kMapHdr10MaxCll        = Name(kRecoveryMapPrefix, "HDR10MaxCLL");

const string kMapSt2086Metadata     = Name(kRecoveryMapPrefix, "ST2086Metadata");
const string kMapSt2086MaxLum       = Name(kRecoveryMapPrefix, "ST2086MaxLuminance");
const string kMapSt2086MinLum       = Name(kRecoveryMapPrefix, "ST2086MinLuminance");
const string kMapSt2086Primary      = Name(kRecoveryMapPrefix, "ST2086Primary");
const string kMapSt2086Coordinate   = Name(kRecoveryMapPrefix, "ST2086Coordinate");
const string kMapSt2086CoordinateX  = Name(kRecoveryMapPrefix, "ST2086CoordinateX");
const string kMapSt2086CoordinateY  = Name(kRecoveryMapPrefix, "ST2086CoordinateY");

// RecoveryMap XMP constants - element and attribute values
const int kSt2086PrimaryRed       = 0;
const int kSt2086PrimaryGreen     = 1;
const int kSt2086PrimaryBlue      = 2;
const int kSt2086PrimaryWhite     = 3;

// RecoveryMap XMP constants - names for XMP handlers
const string XMPXmlHandler::rangeScalingFactorAttrName = kMapRangeScalingFactor;
const string XMPXmlHandler::transferFunctionAttrName = kMapTransferFunction;

bool getMetadataFromXMP(uint8_t* xmp_data, size_t xmp_size, jpegr_metadata* metadata) {
    string nameSpace = "http://ns.adobe.com/xap/1.0/\0";

    if (xmp_size < nameSpace.size()+2) {
        // Data too short
        return false;
    }

    if (strncmp(reinterpret_cast<char*>(xmp_data), nameSpace.c_str(), nameSpace.size())) {
        // Not correct namespace
        return false;
    }

    // Position the pointers to the start of XMP XML portion
    xmp_data += nameSpace.size()+1;
    xmp_size -= nameSpace.size()+1;
    XMPXmlHandler handler;

    // We need to remove tail data until the closing tag. Otherwise parser will throw an error.
    while(xmp_data[xmp_size-1]!='>' && xmp_size > 1) {
        xmp_size--;
    }

    string str(reinterpret_cast<const char*>(xmp_data), xmp_size);
    MessageHandler msg_handler;
    unique_ptr<XmlRule> rule(new XmlElementRule);
    XmlReader reader(&handler, &msg_handler);
    reader.StartParse(std::move(rule));
    reader.Parse(str);
    reader.FinishParse();
    if (reader.HasErrors()) {
        // Parse error
        return false;
    }

    if (!handler.getRangeScalingFactor(&metadata->rangeScalingFactor)) {
        return false;
    }

    if (!handler.getTransferFunction(&metadata->transferFunction)) {
        return false;
    }
    return true;
}

string generateXmp(int secondary_image_length, jpegr_metadata& metadata) {
  const vector<string> kConDirSeq({kConDirectory, string("rdf:Seq")});
  const vector<string> kLiItem({string("rdf:li"), kConItem});

  std::stringstream ss;
  photos_editing_formats::image_io::XmlWriter writer(ss);
  writer.StartWritingElement("x:xmpmeta");
  writer.WriteXmlns("x", "adobe:ns:meta/");
  writer.WriteAttributeNameAndValue("x:xmptk", "Adobe XMP Core 5.1.2");
  writer.StartWritingElement("rdf:RDF");
  writer.WriteXmlns("rdf", "http://www.w3.org/1999/02/22-rdf-syntax-ns#");
  writer.StartWritingElement("rdf:Description");
  writer.WriteXmlns(kContainerPrefix, kContainerUri);
  writer.WriteXmlns(kRecoveryMapPrefix, kRecoveryMapUri);
  writer.WriteElementAndContent(kConVersion, kGContainerVersion);
  writer.StartWritingElements(kConDirSeq);
  size_t item_depth = writer.StartWritingElements(kLiItem);
  writer.WriteAttributeNameAndValue(kConItemSemantic, kSemanticPrimary);
  writer.WriteAttributeNameAndValue(kConItemMime, kMimeImageJpeg);
  writer.WriteAttributeNameAndValue(kMapVersion, metadata.version);
  writer.WriteAttributeNameAndValue(kMapRangeScalingFactor, metadata.rangeScalingFactor);
  writer.WriteAttributeNameAndValue(kMapTransferFunction, metadata.transferFunction);
  if (metadata.transferFunction == JPEGR_TF_PQ) {
    writer.StartWritingElement(kMapHdr10Metadata);
    writer.WriteAttributeNameAndValue(kMapHdr10MaxFall, metadata.hdr10Metadata.maxFALL);
    writer.WriteAttributeNameAndValue(kMapHdr10MaxCll, metadata.hdr10Metadata.maxCLL);
    writer.StartWritingElement(kMapSt2086Metadata);
    writer.WriteAttributeNameAndValue(
        kMapSt2086MaxLum, metadata.hdr10Metadata.st2086Metadata.maxLuminance);
    writer.WriteAttributeNameAndValue(
        kMapSt2086MinLum, metadata.hdr10Metadata.st2086Metadata.minLuminance);

    // red
    writer.StartWritingElement(kMapSt2086Coordinate);
    writer.WriteAttributeNameAndValue(kMapSt2086Primary, kSt2086PrimaryRed);
    writer.WriteAttributeNameAndValue(
        kMapSt2086CoordinateX, metadata.hdr10Metadata.st2086Metadata.redPrimary.x);
    writer.WriteAttributeNameAndValue(
        kMapSt2086CoordinateY, metadata.hdr10Metadata.st2086Metadata.redPrimary.y);
    writer.FinishWritingElement();

    // green
    writer.StartWritingElement(kMapSt2086Coordinate);
    writer.WriteAttributeNameAndValue(kMapSt2086Primary, kSt2086PrimaryGreen);
    writer.WriteAttributeNameAndValue(
        kMapSt2086CoordinateX, metadata.hdr10Metadata.st2086Metadata.greenPrimary.x);
    writer.WriteAttributeNameAndValue(
        kMapSt2086CoordinateY, metadata.hdr10Metadata.st2086Metadata.greenPrimary.y);
    writer.FinishWritingElement();

    // blue
    writer.StartWritingElement(kMapSt2086Coordinate);
    writer.WriteAttributeNameAndValue(kMapSt2086Primary, kSt2086PrimaryBlue);
    writer.WriteAttributeNameAndValue(
        kMapSt2086CoordinateX, metadata.hdr10Metadata.st2086Metadata.bluePrimary.x);
    writer.WriteAttributeNameAndValue(
        kMapSt2086CoordinateY, metadata.hdr10Metadata.st2086Metadata.bluePrimary.y);
    writer.FinishWritingElement();

    // white
    writer.StartWritingElement(kMapSt2086Coordinate);
    writer.WriteAttributeNameAndValue(kMapSt2086Primary, kSt2086PrimaryWhite);
    writer.WriteAttributeNameAndValue(
        kMapSt2086CoordinateX, metadata.hdr10Metadata.st2086Metadata.whitePoint.x);
    writer.WriteAttributeNameAndValue(
        kMapSt2086CoordinateY, metadata.hdr10Metadata.st2086Metadata.whitePoint.y);
    writer.FinishWritingElement();
  }
  writer.FinishWritingElementsToDepth(item_depth);
  writer.StartWritingElements(kLiItem);
  writer.WriteAttributeNameAndValue(kConItemSemantic, kSemanticRecoveryMap);
  writer.WriteAttributeNameAndValue(kConItemMime, kMimeImageJpeg);
  writer.WriteAttributeNameAndValue(kConItemLength, secondary_image_length);
  writer.FinishWriting();

  return ss.str();
}

/*
 * Helper function
 * Add J R entry to existing exif, or create a new one with J R entry if it's null.
 */
status_t updateExif(jr_exif_ptr exif, jr_exif_ptr dest) {
  if (exif == nullptr || exif->data == nullptr) {
    uint8_t data[PSEUDO_EXIF_PACKAGE_LENGTH] = {
        0x45, 0x78, 0x69, 0x66, 0x00, 0x00,
        0x49, 0x49, 0x2A, 0x00,
        0x08, 0x00, 0x00, 0x00,
        0x01, 0x00,
        0x4A, 0x52,
        0x07, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00};
    int pos = 0;
    Write(dest, data, PSEUDO_EXIF_PACKAGE_LENGTH, pos);
    return NO_ERROR;
  }

  int num_entry = 0;
  uint8_t num_entry_low = 0;
  uint8_t num_entry_high = 0;
  bool use_big_endian = false;
  if (reinterpret_cast<uint16_t*>(exif->data)[3] == 0x4949) {
      num_entry_low = reinterpret_cast<uint8_t*>(exif->data)[14];
      num_entry_high = reinterpret_cast<uint8_t*>(exif->data)[15];
  } else if (reinterpret_cast<uint16_t*>(exif->data)[3] == 0x4d4d) {
      use_big_endian = true;
      num_entry_high = reinterpret_cast<uint8_t*>(exif->data)[14];
      num_entry_low = reinterpret_cast<uint8_t*>(exif->data)[15];
  } else {
      return ERROR_JPEGR_METADATA_ERROR;
  }
  num_entry = (num_entry_high << 8) | num_entry_low;
  num_entry += 1;
  num_entry_low = num_entry & 0xff;
  num_entry_high = (num_entry >> 8) & 0xff;

  int pos = 0;
  Write(dest, (uint8_t*)exif->data, 14, pos);

  if (use_big_endian) {
    Write(dest, &num_entry_high, 1, pos);
    Write(dest, &num_entry_low, 1, pos);
    uint8_t data[EXIF_J_R_ENTRY_LENGTH] = {
          0x4A, 0x52,
          0x00, 0x07,
          0x00, 0x00, 0x00, 0x01,
          0x00, 0x00, 0x00, 0x00};
    Write(dest, data, EXIF_J_R_ENTRY_LENGTH, pos);
  } else {
    Write(dest, &num_entry_low, 1, pos);
    Write(dest, &num_entry_high, 1, pos);
    uint8_t data[EXIF_J_R_ENTRY_LENGTH] = {
          0x4A, 0x52,
          0x07, 0x00,
          0x01, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00};
    Write(dest, data, EXIF_J_R_ENTRY_LENGTH, pos);
  }

  Write(dest, (uint8_t*)exif->data + 16, exif->length - 16, pos);

  updateExifOffsets(dest,
                    28, // start from the second tag, skip the "JR" tag
                    num_entry - 1,
                    use_big_endian);

  return NO_ERROR;
}

/*
 * Helper function
 * Modify offsets in EXIF in place.
 */
void updateExifOffsets(jr_exif_ptr exif, int pos, bool use_big_endian) {
  int num_entry = readValue(reinterpret_cast<uint8_t*>(exif->data), pos, 2, use_big_endian);
  updateExifOffsets(exif, pos + 2, num_entry, use_big_endian);
}

void updateExifOffsets(jr_exif_ptr exif, int pos, int num_entry, bool use_big_endian) {
  for (int i = 0; i < num_entry; pos += EXIF_J_R_ENTRY_LENGTH, i++) {
    int tag = readValue(reinterpret_cast<uint8_t*>(exif->data), pos, 2, use_big_endian);
    bool need_to_update_offset = false;
    if (tag == 0x8769) {
      need_to_update_offset = true;
      int sub_ifd_offset =
              readValue(reinterpret_cast<uint8_t*>(exif->data), pos + 8, 4, use_big_endian)
              + 6  // "Exif\0\0";
              + EXIF_J_R_ENTRY_LENGTH;
      updateExifOffsets(exif, sub_ifd_offset, use_big_endian);
    } else {
      int data_format =
              readValue(reinterpret_cast<uint8_t*>(exif->data), pos + 2, 2, use_big_endian);
      int num_of_components =
              readValue(reinterpret_cast<uint8_t*>(exif->data), pos + 4, 4, use_big_endian);
      int data_length = findFormatLengthInBytes(data_format) * num_of_components;
      if (data_length > 4) {
        need_to_update_offset = true;
      }
    }

    if (!need_to_update_offset) {
      continue;
    }

    int offset = readValue(reinterpret_cast<uint8_t*>(exif->data), pos + 8, 4, use_big_endian);

    offset += EXIF_J_R_ENTRY_LENGTH;

    if (use_big_endian) {
      reinterpret_cast<uint8_t*>(exif->data)[pos + 11] = offset & 0xff;
      reinterpret_cast<uint8_t*>(exif->data)[pos + 10] = (offset >> 8) & 0xff;
      reinterpret_cast<uint8_t*>(exif->data)[pos + 9] = (offset >> 16) & 0xff;
      reinterpret_cast<uint8_t*>(exif->data)[pos + 8] = (offset >> 24) & 0xff;
    } else {
      reinterpret_cast<uint8_t*>(exif->data)[pos + 8] = offset & 0xff;
      reinterpret_cast<uint8_t*>(exif->data)[pos + 9] = (offset >> 8) & 0xff;
      reinterpret_cast<uint8_t*>(exif->data)[pos + 10] = (offset >> 16) & 0xff;
      reinterpret_cast<uint8_t*>(exif->data)[pos + 11] = (offset >> 24) & 0xff;
    }
  }
}

/*
 * Read data from the target position and target length in bytes;
 */
int readValue(uint8_t* data, int pos, int length, bool use_big_endian) {
  if (length == 2) {
    if (use_big_endian) {
      return (data[pos] << 8) | data[pos + 1];
    } else {
      return (data[pos + 1] << 8) | data[pos];
    }
  } else if (length == 4) {
    if (use_big_endian) {
      return (data[pos] << 24) | (data[pos + 1] << 16) | (data[pos + 2] << 8) | data[pos + 3];
    } else {
      return (data[pos + 3] << 24) | (data[pos + 2] << 16) | (data[pos + 1] << 8) | data[pos];
    }
  } else {
    // Not support for now.
    ALOGE("Error in readValue(): pos=%d, length=%d", pos, length);
    return -1;
  }
}

/*
 * Helper function
 * Returns the length of data format in bytes
 */
int findFormatLengthInBytes(int data_format) {
  switch (data_format) {
    case 1:  // unsigned byte
    case 2:  // ascii strings
    case 6:  // signed byte
    case 7:  // undefined
      return 1;

    case 3:  // unsigned short
    case 8:  // signed short
      return 2;

    case 4:  // unsigned long
    case 9:  // signed long
    case 11:  // single float
      return 4;

    case 5:  // unsigned rational
    case 10:  // signed rational
    case 12:  // double float
      return 8;

    default:
      // should not hit here
      ALOGE("Error in findFormatLengthInBytes(): data_format=%d", data_format);
      return -1;
  }
}

} // namespace android::recoverymap