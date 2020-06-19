// Copyright (c) 2018 Microsoft Corporation
// Licensed under the MIT license.
// Author: Paul Koch <code@koch.ninja>

#include "PrecompiledHeader.h"

#include <stdlib.h> // free
#include <stddef.h> // size_t, ptrdiff_t

#include "ebm_native.h" // FloatEbmType
#include "EbmInternal.h" // FeatureType
#include "Logging.h" // EBM_ASSERT & LOG
#include "Feature.h"
#include "DataSetInteraction.h"
#include "InitializeResiduals.h"

EBM_INLINE static FloatEbmType * ConstructResidualErrors(
   const size_t cInstances, 
   const void * const aTargetData, 
   const FloatEbmType * const aPredictorScores, 
   const ptrdiff_t runtimeLearningTypeOrCountTargetClasses
) {
   LOG_0(TraceLevelInfo, "Entered DataSetByFeature::ConstructResidualErrors");

   EBM_ASSERT(1 <= cInstances);
   EBM_ASSERT(nullptr != aTargetData);
   EBM_ASSERT(nullptr != aPredictorScores);

   const size_t cVectorLength = GetVectorLength(runtimeLearningTypeOrCountTargetClasses);
   EBM_ASSERT(1 <= cVectorLength);

   if(IsMultiplyError(cInstances, cVectorLength)) {
      LOG_0(TraceLevelWarning, "WARNING DataSetByFeature::ConstructResidualErrors IsMultiplyError(cInstances, cVectorLength)");
      return nullptr;
   }

   const size_t cElements = cInstances * cVectorLength;
   FloatEbmType * aResidualErrors = EbmMalloc<FloatEbmType, false>(cElements);

   if(IsClassification(runtimeLearningTypeOrCountTargetClasses)) {
      if(IsBinaryClassification(runtimeLearningTypeOrCountTargetClasses)) {
         InitializeResiduals<2>::Func(
            cInstances, 
            aTargetData, 
            aPredictorScores, 
            aResidualErrors, 
            ptrdiff_t { 2 },
            nullptr
         );
      } else {
         FloatEbmType * const aTempFloatVector = EbmMalloc<FloatEbmType, false>(cVectorLength);
         if(UNLIKELY(nullptr == aTempFloatVector)) {
            LOG_0(TraceLevelWarning, "WARNING DataSetByFeature::ConstructResidualErrors nullptr == aTempFloatVector");
            free(aResidualErrors);
            return nullptr;
         }
         InitializeResiduals<k_DynamicClassification>::Func(
            cInstances, 
            aTargetData, 
            aPredictorScores, 
            aResidualErrors, 
            runtimeLearningTypeOrCountTargetClasses,
            aTempFloatVector
         );
         free(aTempFloatVector);
      }
   } else {
      EBM_ASSERT(IsRegression(runtimeLearningTypeOrCountTargetClasses));
      InitializeResiduals<k_Regression>::Func(cInstances, aTargetData, aPredictorScores, aResidualErrors, k_Regression, nullptr);
   }

   LOG_0(TraceLevelInfo, "Exited DataSetByFeature::ConstructResidualErrors");
   return aResidualErrors;
}

EBM_INLINE static StorageDataType * * ConstructInputData(
   const size_t cFeatures, 
   const Feature * const aFeatures, 
   const size_t cInstances, 
   const IntEbmType * 
   const aBinnedData
) {
   LOG_0(TraceLevelInfo, "Entered DataSetByFeature::ConstructInputData");

   EBM_ASSERT(0 < cFeatures);
   EBM_ASSERT(nullptr != aFeatures);
   EBM_ASSERT(0 < cInstances);
   EBM_ASSERT(nullptr != aBinnedData);

   StorageDataType ** const aaInputDataTo = EbmMalloc<StorageDataType *, false>(cFeatures);
   if(nullptr == aaInputDataTo) {
      LOG_0(TraceLevelWarning, "WARNING DataSetByFeature::ConstructInputData nullptr == aaInputDataTo");
      return nullptr;
   }

   StorageDataType ** paInputDataTo = aaInputDataTo;
   const Feature * pFeature = aFeatures;
   const Feature * const pFeatureEnd = aFeatures + cFeatures;
   do {
      StorageDataType * pInputDataTo = EbmMalloc<StorageDataType, false>(cInstances);
      if(nullptr == pInputDataTo) {
         LOG_0(TraceLevelWarning, "WARNING DataSetByFeature::ConstructInputData nullptr == pInputDataTo");
         goto free_all;
      }
      *paInputDataTo = pInputDataTo;
      ++paInputDataTo;

      const IntEbmType * pInputDataFrom = &aBinnedData[pFeature->GetIndexFeatureData() * cInstances];
      const IntEbmType * pInputDataFromEnd = &pInputDataFrom[cInstances];
      do {
         const IntEbmType data = *pInputDataFrom;
         EBM_ASSERT(0 <= data);
         EBM_ASSERT((IsNumberConvertable<size_t, IntEbmType>(data))); // data must be lower than cBins and cBins fits into a size_t which we checked earlier
         EBM_ASSERT(static_cast<size_t>(data) < pFeature->GetCountBins());
         EBM_ASSERT((IsNumberConvertable<StorageDataType, IntEbmType>(data)));
         *pInputDataTo = static_cast<StorageDataType>(data);
         ++pInputDataTo;
         ++pInputDataFrom;
      } while(pInputDataFromEnd != pInputDataFrom);

      ++pFeature;
   } while(pFeatureEnd != pFeature);

   LOG_0(TraceLevelInfo, "Exited DataSetByFeature::ConstructInputData");
   return aaInputDataTo;

free_all:
   while(aaInputDataTo != paInputDataTo) {
      --paInputDataTo;
      free(*paInputDataTo);
   }
   free(aaInputDataTo);
   return nullptr;
}

void DataSetByFeature::Destruct() {
   LOG_0(TraceLevelInfo, "Entered DataSetByFeature::Destruct");

   free(m_aResidualErrors);
   if(nullptr != m_aaInputData) {
      EBM_ASSERT(1 <= m_cFeatures);
      StorageDataType ** paInputData = m_aaInputData;
      const StorageDataType * const * const paInputDataEnd = m_aaInputData + m_cFeatures;
      do {
         EBM_ASSERT(nullptr != *paInputData);
         free(*paInputData);
         ++paInputData;
      } while(paInputDataEnd != paInputData);
      free(m_aaInputData);
   }

   LOG_0(TraceLevelInfo, "Exited DataSetByFeature::Destruct");
}

bool DataSetByFeature::Initialize(
   const size_t cFeatures, 
   const Feature * const aFeatures, 
   const size_t cInstances, 
   const IntEbmType * const aBinnedData, 
   const void * const aTargetData, 
   const FloatEbmType * const aPredictorScores, 
   const ptrdiff_t runtimeLearningTypeOrCountTargetClasses
) {
   EBM_ASSERT(nullptr == m_aResidualErrors); // we expect to start with zeroed values
   EBM_ASSERT(nullptr == m_aaInputData); // we expect to start with zeroed values
   EBM_ASSERT(0 == m_cInstances); // we expect to start with zeroed values

   LOG_0(TraceLevelInfo, "Entered DataSetByFeature::Initialize");

   if(0 != cInstances) {
      // if cInstances is zero, then we don't need to allocate anything since we won't use them anyways

      FloatEbmType * aResidualErrors = ConstructResidualErrors(cInstances, aTargetData, aPredictorScores, runtimeLearningTypeOrCountTargetClasses);
      if(nullptr == aResidualErrors) {
         goto exit_error;
      }
      if(0 != cFeatures) {
         StorageDataType ** const aaInputData = ConstructInputData(cFeatures, aFeatures, cInstances, aBinnedData);
         if(nullptr == aaInputData) {
            free(aResidualErrors);
            goto exit_error;
         }
         m_aaInputData = aaInputData;
      }
      m_aResidualErrors = aResidualErrors;
      m_cInstances = cInstances;
   }
   m_cFeatures = cFeatures;

   LOG_0(TraceLevelInfo, "Exited DataSetByFeature::Initialize");
   return false;

exit_error:;
   LOG_0(TraceLevelWarning, "WARNING Exited DataSetByFeature::Initialize");
   return true;
}