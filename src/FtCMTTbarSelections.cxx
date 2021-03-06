#include "UHH2/FtCMTTbar/include/FtCMTTbarSelections.h"

#include <stdexcept>
#include <iostream>
#include <memory>

#include "UHH2/core/include/AnalysisModule.h"
#include "UHH2/core/include/Event.h"
#include "UHH2/core/include/Selection.h"
#include "UHH2/core/include/LorentzVector.h"

#include "UHH2/common/include/NSelections.h"
#include "UHH2/common/include/ObjectIdUtils.h"
#include "UHH2/common/include/JetIds.h"
#include "UHH2/common/include/TopJetIds.h"
#include "UHH2/common/include/Utils.h"

using namespace uhh2;

HTlepCut::HTlepCut(float min_htlep, float max_htlep):
  min_htlep_(min_htlep), max_htlep_(max_htlep) {}

bool HTlepCut::passes(const Event & event){

  assert(event.muons || event.electrons);
  assert(event.met);

  float plep_pt(0.);

  if(event.electrons){
    for(const auto & ele : *event.electrons){
      if(ele.pt() > plep_pt) plep_pt = ele.pt();
    }
  }

  if(event.muons) {
    for(const auto & mu : *event.muons){
      if(mu.pt() > plep_pt) plep_pt = mu.pt();
    }
  }

  float htlep = plep_pt + event.met->pt();
  return (htlep > min_htlep_) && (htlep < max_htlep_);
}
////////////////////////////////////////////////////////

METCut::METCut(float min_met, float max_met):
  min_met_(min_met), max_met_(max_met) {}

bool METCut::passes(const Event & event){

  assert(event.met);

  float MET = event.met->pt();
  return (MET > min_met_) && (MET < max_met_);
}
////////////////////////////////////////////////////////

NJetCut::NJetCut(int nmin_, int nmax_, float ptmin_, float etamax_):
  nmin(nmin_), nmax(nmax_), ptmin(ptmin_), etamax(etamax_) {}

bool NJetCut::passes(const Event & event){

  int njet(0);
  for(auto & jet : *event.jets){
    if(jet.pt() > ptmin && fabs(jet.eta()) < etamax) ++njet;
  }

  return (njet >= nmin) && (njet <= nmax);
}
////////////////////////////////////////////////////////

bool TwoDCut::passes(const Event & event){

  assert(event.muons && event.electrons && event.jets);
  /*  if((event.muons->size()+event.electrons->size()) != 1){
    std::cout << "N_elec=" << event.electrons->size() << "N_muon=" << event.muons->size() << "\n @@@ WARNING -- TwoDCut::passes -- unexpected number of muons+electrons in the event (!=1). returning 'false'\n";
    return false;
    }*/

  float drmin, ptrel;  
  if(event.muons->size()) std::tie(drmin, ptrel) = drmin_pTrel(event.muons->at(0), *event.jets);
  else std::tie(drmin, ptrel) = drmin_pTrel(event.electrons->at(0), *event.jets);

  return (drmin > min_deltaR_) || (ptrel > min_pTrel_);
}
////////////////////////////////////////////////////////

TriangularCuts::TriangularCuts(float a, float b): a_(a), b_(b) {

  if(!b_) std::runtime_error("TriangularCuts -- incorrect initialization (parameter 'b' is null)");
}

bool TriangularCuts::passes(const Event & event){

  assert(event.muons || event.electrons);
  assert(event.jets && event.met);

  if((event.muons->size()+event.electrons->size()) != 1){
    std::cout << "\n @@@ WARNING -- TriangularCuts::passes -- unexpected number of muons+electrons in the event (!=1). returning 'false'\n";
    return false;
  }

  if(!event.jets->size()){
    std::cout << "\n @@@ WARNING -- TriangularCuts::passes -- unexpected number of jets in the event (==0). returning 'false'\n";
    return false;
  }

  // charged lepton
  const Particle* lep1(0);
  if(event.muons->size()) lep1 = &event.muons->at(0);
  else lep1 = &event.electrons->at(0);

  // 1st entry in jet collection (should be the pt-leading jet)
  const Particle* jet1 = &event.jets->at(0);

  // MET-lepton triangular cut
  bool pass_tc_lep = fabs(fabs(deltaPhi(*event.met, *lep1)) - a_) < a_/b_ * event.met->pt();

  // MET-jet triangular cut
  bool pass_tc_jet = fabs(fabs(deltaPhi(*event.met, *jet1)) - a_) < a_/b_ * event.met->pt();

  return pass_tc_lep && pass_tc_jet;
}
////////////////////////////////////////////////////////

TopTagEventSelection::TopTagEventSelection(const TopJetId& tjetID, float minDR_jet_ttag):
  topjetID_(tjetID), minDR_jet_toptag_(minDR_jet_ttag) {

  topjet1_sel_.reset(new NTopJetSelection(1, -1, topjetID_));
}

bool TopTagEventSelection::passes(const Event & event){ 

  if(!topjet1_sel_->passes(event)) return false;

  for(auto & topjet : * event.topjets){
    if(!topjetID_(topjet, event)) continue;

    for(auto & jet : * event.jets)
      if(deltaR(jet, topjet) > minDR_jet_toptag_) return true;
  }

  return false;
}
////////////////////////////////////////////////////////

HypothesisDiscriminatorCut::HypothesisDiscriminatorCut(uhh2::Context& ctx, float min_discr, float max_discr, const std::string& discr_name, const std::string& hyps_name):
  m_min_discr_(min_discr), m_max_discr_(max_discr), m_discriminator_name(discr_name), h_hyps(ctx.get_handle<std::vector<ReconstructionHypothesis>>(hyps_name)) {}

bool HypothesisDiscriminatorCut::passes(const Event & event){

  std::vector<ReconstructionHypothesis> hyps = event.get(h_hyps);
  const ReconstructionHypothesis* hyp = get_best_hypothesis( hyps, m_discriminator_name);

  if(!hyp){
//    std::cout << "WARNING: no hypothesis " << m_discr->GetLabel() << " found, event is rejected.\n";
    return false;
  }

  float discr_value = hyp->discriminator(m_discriminator_name);
  if(discr_value < m_min_discr_ || discr_value > m_max_discr_) return false;

  return true;
}
////////////////////////////////////////////////////////

NMuonBTagSelection::NMuonBTagSelection(int min_nbtag, int max_nbtag, JetId btag, double ptmin, double etamax )
{
  m_min_nbtag=min_nbtag;
  m_max_nbtag=max_nbtag;
  m_btag=btag;
  m_ptmin=ptmin;
  m_etamax=etamax;
}

bool NMuonBTagSelection::passes(const Event & event)
{
  int nbtag=0;

  //Assumes to have only one muon                                                                  
  std::vector<Jet>* jets = event.jets;
  std::vector<Muon>* muons = event.muons;
  for(unsigned int i=0; i<event.jets->size(); ++i) {
    int jettagged=0;
    Jet jet=jets->at(i);
    if (m_btag(jet, event)) jettagged=1;
 
    if(muons->size() != 1){
      std::cout << "ATTENTION!!! muon size " << muons->size() << std::endl;
    }

    double deltaphi=deltaPhi(jet,muons->at(0));
    double pi = 3.14159265359;
    if(jettagged&&(deltaphi<(2*pi/3))&&(jet.pt()>m_ptmin)&&(fabs(jet.eta())<m_etamax)){

      nbtag++;

    }

  }

  if(nbtag<m_min_nbtag) return false;
  if(nbtag>m_max_nbtag) return false;
  return true;
}
////////////////////////////////////////////////////////

SubBTagSelection::SubBTagSelection(int min_nsubbtag, int max_nsubbtag, JetId subbtag, double ptsubmin, double etasubmax )
{
    m_min_nsubbtag=min_nsubbtag;
    m_max_nsubbtag=max_nsubbtag;
    m_subbtag=subbtag;
    m_ptsubmin=ptsubmin;
    m_etasubmax=etasubmax;
}

bool SubBTagSelection::passes(const Event & event)
{
  int nsubbtag=0;

  //Assumes to have only one muon                                                                  
  std::vector<TopJet>* topjets = event.topjets;
  std::vector<Muon>* muons = event.muons;     
  int subjettagged=0;

  for(unsigned int j=0;j<topjets->size();j++){
    subjettagged=0;
    TopJet topjet=topjets->at(j);
    if(muons->size() != 1){
      std::cout << "ATTENTION!!! muon size " << muons->size() << std::endl;}
    double deltaphi=deltaPhi(topjet,muons->at(0));
    double pi = 3.14159265359;
    if((deltaphi<(2*pi/3))&&(topjet.pt()>m_ptsubmin)&&(fabs(topjet.eta())<m_etasubmax)){
      const std::vector<Jet> subjets=topjet.subjets();
      for(unsigned int ii=0;ii<subjets.size();ii++){
	Jet subjet=subjets[ii];
	if (m_subbtag(subjet, event)) subjettagged=1;
	//subjettagged=1;
      }
      if (subjettagged)nsubbtag++;
      
    }
  }
  if(nsubbtag<m_min_nsubbtag) return false;
  if(nsubbtag>m_max_nsubbtag) return false;
  return true;
  
}
////////////////////////////////////////////////////////
