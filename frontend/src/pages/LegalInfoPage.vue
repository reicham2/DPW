<template>
	<PublicPageLayout :label="title">
		<article class="legal-document">
			<h1>{{ title }}</h1>

			<template v-if="kind === 'impressum'">
				<section>
					<h2>Betreiberin</h2>
					<p>
						Pfadi Hünenberg (Pfadi Hü)<br />
						Pfadiheim Hünenberg<br />
						Rigistrasse 1<br />
						6331 Hünenberg<br />
						Schweiz
					</p>
					<p>
						E-Mail:
						<a href="mailto:al@pfadihue.ch">al@pfadihue.ch</a>
					</p>
				</section>

				<section>
					<h2>Inhalt und Kontakt</h2>
					<p>
						DPWeb unterstützt die Planung von Pfadiaktivitäten und stellt
						Funktionen für öffentliche Aktivitätsfreigaben und Formulare bereit.
						Anfragen zur Anwendung kannst du an die oben genannte Kontaktadresse
						richten.
					</p>
				</section>
			</template>

			<template v-else>
				<p class="legal-draft-note">
					Diese Datenschutzhinweise sind ein Organisationsentwurf. Die
					verantwortliche Stelle muss sie vor dem öffentlichen Einsatz mit der
					tatsächlichen Konfiguration dieser DPWeb-Instanz abgleichen.
				</p>

				<section>
					<h2>Verantwortliche Stelle</h2>
					<p>
						Pfadi Hünenberg (Pfadi Hü)<br />
						Pfadiheim Hünenberg<br />
						Rigistrasse 1, 6331 Hünenberg, Schweiz<br />
						<a href="mailto:al@pfadihue.ch">al@pfadihue.ch</a>
					</p>
				</section>

				<section>
					<h2>Welche Daten DPWeb verarbeitet</h2>
					<p>
						DPWeb dient der Planung und Verwaltung von Aktivitäten. Dazu gehören
						Benutzerangaben wie Microsoft-Kennung, E-Mail-Adresse und Anzeigename,
						sowie Aktivitäts-, Programm-, Verantwortlichkeits- und Materialangaben,
						Notizen, Anhänge, eingefügte Bilder, Orte und Wetterangaben.
						Benutzerkonten enthalten zusätzlich Rollen, Abteilungen und
						Einstellungen für die Nutzung und Benachrichtigungen. DPWeb speichert
						auch Formular- und Mailvorlagen, Mailentwürfe und Protokolle
						gesendeter E-Mails mit Absender, Empfängern, Betreff, Inhalt und
						Versandzeitpunkt. In-App-Benachrichtigungen enthalten Titel, Nachricht
						und gegebenenfalls einen Link zur Aktivität.
					</p>
					<p>
						Bei einer öffentlichen Formularantwort speichert DPWeb die eingegebenen
						Antworten und den Absendezeitpunkt. Zusätzlich werden, sofern vom
						Webserver mitgeliefert, die IP-Adresse aus dem Header «X-Real-IP» und
						der Browser-User-Agent erfasst. Bitte übermittle nur Angaben, die für
						das jeweilige Formular erforderlich sind.
					</p>
					<p>
						Bei angemeldeten Benutzern protokolliert das Audit-Log bestimmte
						Änderungen mit Benutzerkennung, Zeitpunkt, Aktion und begrenzten
						Metadaten. Es enthält keine vollständigen Formularantworten oder
						Nachrichteninhalte. Die Standard-Aufbewahrungsdauer beträgt 14 Tage
						und kann durch Administratoren angepasst werden.
					</p>
				</section>

				<section>
					<h2>Öffentliche Freigaben und Formulare</h2>
					<p>
						Geteilte Aktivitäten sind für alle Personen mit dem Freigabelink ohne
						Anmeldung sichtbar. Das umfasst die für die Aktivität freigegebenen
						Inhalte und Programmbilder. Öffentliche Anmelde- und Abmeldeformulare
						können ebenfalls ohne Anmeldung aufgerufen und ausgefüllt werden.
						Teile Links deshalb nur mit den vorgesehenen Empfängerinnen und
						Empfängern.
					</p>
				</section>

				<section>
					<h2>Empfänger und optionale Dienste</h2>
					<p>
						Auf gespeicherte Aktivitäten und Formularantworten können die
						entsprechend berechtigten DPWeb-Benutzer zugreifen. Microsoft Entra ID
						wird für die Anmeldung verwendet. Die Kontaktsuche fragt über Microsoft
						Graph nach passenden Personen und liest deren Anzeigenamen und
						E-Mail-Adressen. Beim E-Mailversand werden Absender, Empfänger,
						Betreff und Inhalt an Microsoft Graph übermittelt und in den
						gesendeten Elementen des Microsoft-Kontos gespeichert. DPWeb speichert
						zusätzlich ein Protokoll mit Absender, Empfängern, Betreff, Inhalt und
						Versandzeitpunkt.
					</p>
					<p>
						Wenn MiData eingerichtet ist, ruft DPWeb für eine Gruppenabfrage die
						MiData-Mitgliederdaten ab und wertet sie für eine Kinderzählung aus.
						DPWeb speichert die ermittelte Zahl bei der Aktivität, nicht die
						MiData-Mitgliederliste. Bei eingerichteter WordPress/EventON-Anbindung
						übermittelt DPWeb für veröffentlichte Veranstaltungen Titel,
						Beschreibung, Beginn, Ende, den Veranstaltungstyp und gegebenenfalls
						den Ort an die konfigurierte WordPress-Instanz.
					</p>
					<p>
						Wenn GitHub-Berichte eingerichtet sind, wird ein Fehlerbericht erst
						nach dem Absenden durch die Benutzerin oder den Benutzer als Issue im
						konfigurierten GitHub-Repository erstellt. Er enthält die Beschreibung,
						die aktuelle Anwendungsadresse, Browserangaben und – bei angemeldeten
						Personen – Name und E-Mail-Adresse des Kontos. Erweiterte
						Diagnosedaten wie zuletzt besuchte Ansichten, Interaktionen,
						API-Aufrufe und Fehlermeldungen werden nur übermittelt, wenn vor dem
						Absenden die standardmässig deaktivierte Option aktiviert wird.
					</p>
					<p>
						Für angeforderte Wetterprognosen lädt DPWeb serverseitig öffentliche
						MeteoSwiss-Prognosedaten von <code>geo.admin.ch</code>. Der eingegebene
						Aktivitätsort wird mit den Prognosepunkten abgeglichen; der
						Wetter-Snapshot wird bei der Aktivität gespeichert. Die Anfragen an
						geo.admin.ch laden öffentliche Datensätze und enthalten nicht den
						eingegebenen Aktivitätsort.
					</p>
					<p>
						Die DPWeb-Instanz wird in der Schweiz von LKlaus Hosting gehostet.
						DPWeb speichert Anwendungsdaten in der für diese Instanz
						eingerichteten Datenbank. Die konkrete Aufbewahrung von
						Serverprotokollen und Sicherungskopien richtet sich nach der
						Konfiguration der Instanz und des Hosting-Anbieters.
					</p>
					<p>
						Die Speicher- und Übermittlungsorte bei Microsoft und anderen
						optional verwendeten Diensten richten sich nach den jeweiligen
						Diensten und der Konfiguration. DPWeb legt diese Orte nicht fest.
					</p>
				</section>

				<section>
					<h2>Speicherung und Löschung</h2>
					<p>
						Formularantworten werden nicht automatisch nach einer festen Frist
						gelöscht. Berechtigte Benutzer können einzelne Antworten löschen;
						beim Löschen des zugehörigen Formulars werden sie ebenfalls entfernt.
						Gelöschte Aktivitäten bleiben zunächst im Papierkorb und können
						wiederhergestellt werden. Sie werden nach 90 Tagen automatisch
						endgültig gelöscht; dabei werden auch die zugehörigen Formulare,
						Antworten, Anhänge, Mailentwürfe und gesendeten E-Mails entfernt.
						Berechtigte Administratoren können Aktivitäten auch vorher endgültig
						löschen. Audit-Einträge werden standardmässig nach 14 Tagen bereinigt;
						die Frist ist durch Administratoren konfigurierbar.
					</p>
					<p>
						Aktivitätsbezogene In-App-Benachrichtigungen werden ab dem Tag nach
						dem Aktivitätsdatum automatisch entfernt. Mail- und Formularentwürfe
						können in DPWeb gelöscht werden; gesendete E-Mail-Protokolle bleiben
						bis zur endgültigen Löschung der zugehörigen Aktivität gespeichert.
					</p>
					<p>
						DPWeb definiert keine allgemeine Aufbewahrungsfrist für
						übrige Anwendungsdaten, Serverprotokolle oder Sicherungskopien. Mail-
						und Formularentwürfe können durch berechtigte Benutzer gelöscht
						werden. Die Aufbewahrung von Serverprotokollen und Sicherungskopien
						hängt von der Verwaltung und Konfiguration dieser Instanz ab. Bei
						Fragen zu Auskunft, Berichtigung oder Löschung deiner Daten wende
						dich an die oben genannte Kontaktadresse.
					</p>
				</section>

				<section>
					<h2>Browser-Speicher und Cookies</h2>
					<p>
						Die Microsoft-Anmeldung speichert ihren Token-Cache im lokalen
						Browser-Speicher. DPWeb verwendet den Browser-Speicher ausserdem für
						Einstellungen und Bearbeitungsentwürfe; einzelne Ansichten verwenden
						den Sitzungsspeicher. DPWeb bindet keine externen Analyse- oder
						Marketingdienste ein. Technische Diagnoseinformationen zu Interaktionen,
						API-Aufrufen und Fehlern werden während der Sitzung im Arbeitsspeicher
						gesammelt und nur mit einem ausdrücklich abgesendeten Fehlerbericht
						übermittelt, wenn die Option für erweiterte Diagnosedaten aktiviert
						wurde. Die Microsoft-Anmeldung ist nicht für die Speicherung ihres
						Zustands in Cookies konfiguriert.
					</p>
					<p>
						Wenn eine Benutzerin oder ein Benutzer Web-Push aktiviert, speichert
						DPWeb das Browser-Abonnement samt Zustellungsdaten auf dem Server und
						sendet Mitteilungstext und Link an den Push-Dienst des Browsers.
					</p>
				</section>

				<section>
					<h2>Kontakt und Aktualisierungen</h2>
					<p>
						Fragen zu diesen Hinweisen oder zu deinen Daten kannst du an
						<a href="mailto:al@pfadihue.ch">al@pfadihue.ch</a> richten.
						Die Hinweise können angepasst werden, wenn sich DPWeb oder die
						Konfiguration dieser Instanz ändert.
					</p>
				</section>
			</template>
		</article>
	</PublicPageLayout>
</template>

<script setup lang="ts">
import { computed, onMounted } from 'vue';
import PublicPageLayout from '../components/PublicPageLayout.vue';

const props = defineProps<{ kind: 'impressum' | 'datenschutz' }>();
const title = computed(() => props.kind === 'impressum' ? 'Impressum' : 'Datenschutzerklärung');

onMounted(() => {
	document.title = `${title.value} – DPWeb`;
});
</script>

<style scoped>
.legal-document {
	max-width: 760px;
	margin: 0 auto;
	padding: 28px 32px;
	background: var(--card-bg);
	border: 1px solid var(--card-border);
	border-radius: 12px;
	box-shadow: 0 1px 6px rgba(0, 0, 0, 0.06);
}

.legal-document h1 {
	margin: 0 0 24px;
	font-size: clamp(1.7rem, 5vw, 2.2rem);
	line-height: 1.2;
}

.legal-document h2 {
	margin: 24px 0 8px;
	font-size: 1.15rem;
	line-height: 1.35;
}

.legal-document p {
	margin: 0 0 12px;
	color: var(--text-secondary);
	overflow-wrap: anywhere;
}

.legal-document a {
	color: var(--accent);
	text-underline-offset: 0.16em;
}

.legal-document a:focus-visible {
	outline: 2px solid var(--accent);
	outline-offset: 3px;
	border-radius: 2px;
}

.legal-draft-note {
	padding: 12px 14px;
	background: var(--bg-hover);
	border-left: 3px solid var(--accent);
	border-radius: 4px;
}

@media (max-width: 599px) {
	.legal-document {
		padding: 20px 18px;
	}
}
</style>
